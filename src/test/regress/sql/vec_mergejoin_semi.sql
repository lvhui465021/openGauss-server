/*
 * This file is used to test the function of ExecVecMergeJoin(): part 3: semi join
 */
set enable_hashjoin=off;
set enable_nestloop=off;
----
--- case 3: MergeJoin Semi Join
----
explain (verbose on, costs off) select * from vector_mergejoin_table_01 A where A.col_int in(select B.col_int from vector_mergejoin_table_02 B left join vector_mergejoin_table_01 using(col_int)) order by 1,2,3,4,5,6,7,8,9,10 limit 50;

select * from vector_mergejoin_table_01 A where A.col_int in(select B.col_int from vector_mergejoin_table_02 B left join vector_mergejoin_table_01 using(col_int)) order by 1,2,3,4,5,6,7,8,9,10 limit 50;
select * from vector_mergejoin_table_01 A where A.col_int in(select B.col_int from vector_mergejoin_table_02 B left join vector_mergejoin_table_01 C on B.col_int = C.col_int) order by 1,2,3,4,5,6,7,8,9,10 limit 50;
select count(*) from vector_mergejoin_table_01 A where A.col_int in(select B.col_int from vector_mergejoin_table_02 B left join vector_mergejoin_table_01 C on B.col_int = C.col_int);
select * from vector_mergejoin_table_02 A where A.col_int in(select B.col_int from vector_mergejoin_table_01 B left join vector_mergejoin_table_02 C on B.col_int = C.col_int) order by 1,2,3,4,5,6,7,8,9,10 limit 50;

