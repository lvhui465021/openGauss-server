/* -------------------------------------------------------------------------
 *
 * nodeLimit.cpp
 *	  Routines to handle limiting of query results where appropriate
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Portions Copyright (c) 1996-2012, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/gausskernel/runtime/executor/nodeLimit.cpp
 *
 * -------------------------------------------------------------------------
 *
 * INTERFACE ROUTINES
 *		ExecLimit		- extract a limited range of tuples
 *		ExecInitLimit	- initialize node and subnodes..
 *		ExecEndLimit	- shutdown node and subnodes
 */
#include "postgres.h"
#include "knl/knl_variable.h"

#include "executor/executor.h"
#include "executor/nodeLimit.h"
#include "nodes/nodeFuncs.h"

static int64 compute_tuples_needed(LimitState *node);

/* ----------------------------------------------------------------
 *		ExecLimit
 *
 *		This is a very simple node which just performs LIMIT/OFFSET
 *		filtering on the stream of tuples returned by a subplan.
 * ----------------------------------------------------------------
 */
TupleTableSlot* ExecLimit(LimitState* node) /* return: a tuple or NULL */
{
    ScanDirection direction;
    TupleTableSlot* slot = NULL;
    PlanState* outer_plan = NULL;

    /*
     * get information from the node
     */
    direction = node->ps.state->es_direction;
    outer_plan = outerPlanState(node);

    /*
     * The main logic is a simple state machine.
     */
    switch (node->lstate) {
        case LIMIT_INITIAL:

            /*
             * First call for this node, so compute limit/offset. (We can't do
             * this any earlier, because parameters from upper nodes will not
             * be set during ExecInitLimit.)  This also sets position = 0 and
             * changes the state to LIMIT_RESCAN.
             */
            recompute_limits(node);

            /* FALL THRU */
        case LIMIT_RESCAN:

            /*
             * If backwards scan, just return NULL without changing state.
             */
            if (!ScanDirectionIsForward(direction))
                return NULL;

            /*
             * Check for empty window; if so, treat like empty subplan.
             */
            if (node->count <= 0 && !node->noCount) {
                node->lstate = LIMIT_EMPTY;
                return NULL;
            }

            /*
             * Fetch rows from subplan until we reach position > offset.
             */
            for (;;) {
                slot = ExecProcNode(outer_plan);
                if (TupIsNull(slot)) {
                    /*
                     * The subplan returns too few tuples for us to produce
                     * any output at all.
                     */
                    node->lstate = LIMIT_EMPTY;
                    return NULL;
                }
                node->subSlot = slot;
                if (++node->position > node->offset)
                    break;
            }

            /*
             * Okay, we have the first tuple of the window.
             */
            node->lstate = LIMIT_INWINDOW;
            break;

        case LIMIT_EMPTY:

            /*
             * The subplan is known to return no tuples (or not more than
             * OFFSET tuples, in general).	So we return no tuples.
             */
            return NULL;

        case LIMIT_INWINDOW:
            if (ScanDirectionIsForward(direction)) {
                /*
                 * Forwards scan, so check for stepping off end of window. If
                 * we are at the end of the window, return NULL without
                 * advancing the subplan or the position variable; but change
                 * the state machine state to record having done so.
                 */
                if (!node->noCount && node->position - node->offset >= node->count) {
                    node->lstate = LIMIT_WINDOWEND;
                    return NULL;
                }

                /*
                 * Get next tuple from subplan, if any.
                 */
                slot = ExecProcNode(outer_plan);
                if (TupIsNull(slot)) {
                    node->lstate = LIMIT_SUBPLANEOF;
                    return NULL;
                }
                node->subSlot = slot;
                node->position++;
            } else {
                /*
                 * Backwards scan, so check for stepping off start of window.
                 * As above, change only state-machine status if so.
                 */
                if (node->position <= node->offset + 1) {
                    node->lstate = LIMIT_WINDOWSTART;
                    return NULL;
                }

                /*
                 * Get previous tuple from subplan; there should be one!
                 */
                slot = ExecProcNode(outer_plan);
                if (TupIsNull(slot))
                    ereport(ERROR,
                        (errcode(ERRCODE_UNEXPECTED_NULL_VALUE),
                            errmodule(MOD_EXECUTOR),
                            errmsg("LIMIT subplan failed to run backwards")));
                node->subSlot = slot;
                node->position--;
            }
            break;

        case LIMIT_SUBPLANEOF:
            if (ScanDirectionIsForward(direction))
                return NULL;

            /*
             * Backing up from subplan EOF, so re-fetch previous tuple; there
             * should be one!  Note previous tuple must be in window.
             */
            slot = ExecProcNode(outer_plan);
            if (TupIsNull(slot))
                ereport(ERROR,
                    (errcode(ERRCODE_UNEXPECTED_NULL_VALUE),
                        errmodule(MOD_EXECUTOR),
                        errmsg("LIMIT subplan failed to run backwards")));
            node->subSlot = slot;
            node->lstate = LIMIT_INWINDOW;
            /* position does not change 'cause we didn't advance it before */
            break;

        case LIMIT_WINDOWEND:
            if (ScanDirectionIsForward(direction))
                return NULL;

            /*
             * Backing up from window end: simply re-return the last tuple
             * fetched from the subplan.
             */
            slot = node->subSlot;
            node->lstate = LIMIT_INWINDOW;
            /* position does not change 'cause we didn't advance it before */
            break;

        case LIMIT_WINDOWSTART:
            if (!ScanDirectionIsForward(direction))
                return NULL;

            /*
             * Advancing after having backed off window start: simply
             * re-return the last tuple fetched from the subplan.
             */
            slot = node->subSlot;
            node->lstate = LIMIT_INWINDOW;
            /* position does not change 'cause we didn't change it before */
            break;

        default:
            ereport(ERROR,
                (errcode(ERRCODE_UNEXPECTED_NODE_STATE),
                    errmodule(MOD_EXECUTOR),
                    errmsg("impossible LIMIT state: %d", (int)node->lstate)));
            slot = NULL; /* keep compiler quiet */
            break;
    }

    /* Return the current tuple */
    Assert(!TupIsNull(slot));

    return slot;
}

/*
 * Evaluate the limit/offset expressions --- done at startup or rescan.
 *
 * This is also a handy place to reset the current-position state info.
 */
void recompute_limits(LimitState* node)
{
    ExprContext* econtext = node->ps.ps_ExprContext;
    Datum val;
    bool is_null = false;

    if (node->limitOffset) {
        val = ExecEvalExprSwitchContext(node->limitOffset, econtext, &is_null, NULL);
        /* Interpret NULL offset as no offset */
        if (is_null) {
            node->offset = 0;
        } else {
            node->offset = DatumGetInt64(val);
            if (node->offset < 0)
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_ROW_COUNT_IN_RESULT_OFFSET_CLAUSE),
                        errmodule(MOD_EXECUTOR),
                        errmsg("OFFSET must not be negative")));
        }
    } else {
        /* No OFFSET supplied */
        node->offset = 0;
    }

    if (node->limitCount) {
        val = ExecEvalExprSwitchContext(node->limitCount, econtext, &is_null, NULL);
        /* Interpret NULL count as no count (LIMIT ALL) */
        if (is_null) {
            node->count = 0;
            node->noCount = true;
        } else {
            node->count = DatumGetInt64(val);
            if (node->count < 0)
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_ROW_COUNT_IN_LIMIT_CLAUSE),
                        errmodule(MOD_EXECUTOR),
                        errmsg("LIMIT must not be negative")));
            node->noCount = false;
        }
    } else {
        /* No COUNT supplied */
        node->count = 0;
        node->noCount = true;
    }

    /* Reset position to start-of-scan */
    node->position = 0;
    node->subSlot = NULL;

    /* Set state-machine state */
    node->lstate = LIMIT_RESCAN;

    /*
     * Notify child node about limit.  Note: think not to "optimize" by
     * skipping ExecSetTupleBound if compute_tuples_needed returns < 0.  We
     * must update the child node anyway, in case this is a rescan and the
     * previous time we got a different result.
     */
    ExecSetTupleBound(compute_tuples_needed(node), outerPlanState(node));
}

/*
 * Compute the maximum number of tuples needed to satisfy this Limit node.
 * Return a negative value if there is not a determinable limit.
 */
static int64 compute_tuples_needed(LimitState *node)
{
    if (node->noCount) {
        return -1;
    }
    /* Note: if this overflows, we'll return a negative value, which is OK */
    return node->count + node->offset;
}

/* ----------------------------------------------------------------
 *		ExecInitLimit
 *
 *		This initializes the limit node state structures and
 *		the node's subplan.
 * ----------------------------------------------------------------
 */
LimitState* ExecInitLimit(Limit* node, EState* estate, int eflags)
{
    LimitState* limit_state = NULL;
    Plan* outer_plan = NULL;

    /* check for unsupported flags */
    Assert(!(eflags & EXEC_FLAG_MARK));

    /*
     * create state structure
     */
    limit_state = makeNode(LimitState);
    limit_state->ps.plan = (Plan*)node;
    limit_state->ps.state = estate;

    limit_state->lstate = LIMIT_INITIAL;

    /*
     * Miscellaneous initialization
     *
     * Limit nodes never call ExecQual or ExecProject, but they need an
     * exprcontext anyway to evaluate the limit/offset parameters in.
     */
    ExecAssignExprContext(estate, &limit_state->ps);

    /*
     * initialize child expressions
     */
    limit_state->limitOffset = ExecInitExpr((Expr*)node->limitOffset, (PlanState*)limit_state);
    limit_state->limitCount = ExecInitExpr((Expr*)node->limitCount, (PlanState*)limit_state);

    /*
     * Tuple table initialization (XXX not actually used...)
     */
    ExecInitResultTupleSlot(estate, &limit_state->ps);

    /*
     * then initialize outer plan
     */
    outer_plan = outerPlan(node);
    outerPlanState(limit_state) = ExecInitNode(outer_plan, estate, eflags);

    /*
     * limit nodes do no projections, so initialize projection info for this
     * node appropriately
     */
    ExecAssignResultTypeFromTL(&limit_state->ps);
    limit_state->ps.ps_ProjInfo = NULL;

    return limit_state;
}

void ExecEndLimit(LimitState* node)
{
    ExecFreeExprContext(&node->ps);
    ExecEndNode(outerPlanState(node));
}

void ExecReScanLimit(LimitState* node)
{
    /*
     * Recompute limit/offset in case parameters changed, and reset the state
     * machine.  We must do this before rescanning our child node, in case
     * it's a Sort that we are passing the parameters down to.
     */
    recompute_limits(node);

    /*
     * if chgParam of subnode is not null then plan will be re-scanned by
     * first ExecProcNode.
     */
    if (node->ps.lefttree->chgParam == NULL)
        ExecReScan(node->ps.lefttree);
}
