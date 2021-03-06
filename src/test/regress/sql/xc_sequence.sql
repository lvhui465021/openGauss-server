--
-- XC_SEQUENCE
--
-- Check of callback mechanisms on GTM

-- Sequence DROP and CREATE
-- Rollback a creation
START TRANSACTION;
CREATE SEQUENCE xc_sequence_1;
SELECT nextval('xc_sequence_1'); -- ok
ROLLBACK;
SELECT nextval('xc_sequence_1'); -- fail
-- Commit a creation
START TRANSACTION;
CREATE SEQUENCE xc_sequence_1;
SELECT nextval('xc_sequence_1'); -- ok
COMMIT;
SELECT nextval('xc_sequence_1'); -- ok
-- Rollback a drop
START TRANSACTION;
DROP SEQUENCE xc_sequence_1;
SELECT nextval('xc_sequence_1'); -- fail
ROLLBACK;
SELECT nextval('xc_sequence_1'); -- ok, previous transaction failed
-- Commit a drop
START TRANSACTION;
DROP SEQUENCE xc_sequence_1;
COMMIT;
SELECT nextval('xc_sequence_1'); -- fail

-- SEQUENCE RENAME TO
-- Rollback a renaming
CREATE SEQUENCE xc_sequence_1;
SELECT nextval('xc_sequence_1'); -- ok
START TRANSACTION;
ALTER SEQUENCE xc_sequence_1 RENAME TO xc_sequence_2;
SELECT nextval('xc_sequence_2');
ROLLBACK;
SELECT nextval('xc_sequence_1'); -- ok
-- Commit a renaming
START TRANSACTION;
ALTER SEQUENCE xc_sequence_1 RENAME TO xc_sequence_2;
SELECT nextval('xc_sequence_2');
COMMIT;
SELECT nextval('xc_sequence_2');
DROP SEQUENCE IF EXISTS xc_sequence_2;
DROP SEQUENCE IF EXISTS xc_sequence_1;

-- Columns with SERIAL
-- Serial sequence is named xc_sequence_tab1_col2_seq
CREATE TABLE xc_sequence_tab1 (col1 int, col2 serial);
-- Some data
INSERT INTO xc_sequence_tab1 VALUES (1, DEFAULT);
INSERT INTO xc_sequence_tab1 VALUES (2, DEFAULT);
SELECT col1, col2 FROM xc_sequence_tab1 ORDER BY 1;
-- Rollback a SERIAL column drop
START TRANSACTION;
ALTER TABLE xc_sequence_tab1 DROP COLUMN col2;
INSERT INTO xc_sequence_tab1 VALUES (3);
SELECT col1 FROM xc_sequence_tab1 ORDER BY 1;
ROLLBACK;
SELECT nextval('xc_sequence_tab1_col2_seq'); -- ok
-- Commit a SERIAL column drop
START TRANSACTION;
ALTER TABLE xc_sequence_tab1 DROP COLUMN col2;
INSERT INTO xc_sequence_tab1 VALUES (3);
SELECT col1 FROM xc_sequence_tab1 ORDER BY 1;
COMMIT;
DROP TABLE xc_sequence_tab1;
-- Need to recreate here, serial column is no more
CREATE TABLE xc_sequence_tab1 (col1 int, col2 serial);
INSERT INTO xc_sequence_tab1 VALUES (1234, DEFAULT);
SELECT col1, col2 FROM xc_sequence_tab1 ORDER BY 1;
-- Rollback of a table with SERIAL
START TRANSACTION;
DROP TABLE xc_sequence_tab1;
ROLLBACK;
SELECT nextval('xc_sequence_tab1_col2_seq'); -- ok
-- Commit of a table with SERIAL
START TRANSACTION;
DROP TABLE xc_sequence_tab1;
COMMIT;
-- Recreate a sequence with the same name as previous SERIAL one
CREATE SEQUENCE xc_sequence_tab1_col2_seq START 2344;
SELECT nextval('xc_sequence_tab1_col2_seq'); -- ok
DROP SEQUENCE xc_sequence_tab1_col2_seq;
-- Create a sequence with negative INCREMENT
CREATE SEQUENCE xc_sequence_neg_set INCREMENT BY -2;
SELECT nextval('xc_sequence_neg_set'); -- ok
SELECT currval('xc_sequence_neg_set'); -- ok
SELECT setval('xc_sequence_neg_set', -99); -- ok
SELECT nextval('xc_sequence_neg_set'); -- ok
SELECT currval('xc_sequence_neg_set'); -- ok
DROP SEQUENCE xc_sequence_neg_set;
-- Alter sequence restart with
CREATE SEQUENCE xc_sequence_restart INCREMENT BY 2 START WITH 1;
SELECT xc_sequence_restart.nextval; -- ok
SELECT xc_sequence_restart.nextval; -- ok
ALTER SEQUENCE xc_sequence_restart RESTART 2;
SELECT xc_sequence_restart.nextval; -- ok
SELECT xc_sequence_restart.nextval; -- ok
ALTER SEQUENCE xc_sequence_restart RESTART WITH 1;
SELECT xc_sequence_restart.nextval; -- ok
SELECT xc_sequence_restart.nextval; -- ok
DROP SEQUENCE xc_sequence_restart;
