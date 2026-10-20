-- NexusIM Stage 6 in-place upgrade. This script is non-destructive: it never
-- drops tables or rows. Run the duplicate preflight queries first. If they
-- return rows, reconcile those duplicates before applying the unique keys.

SELECT `user_id`, `friend_id`, COUNT(*) AS duplicate_count
FROM `Friend`
GROUP BY `user_id`, `friend_id`
HAVING COUNT(*) > 1;

SELECT `group_id`, `user_id`, COUNT(*) AS duplicate_count
FROM `GroupUser`
GROUP BY `group_id`, `user_id`
HAVING COUNT(*) > 1;

-- These operations are idempotent on MySQL versions without
-- ADD CONSTRAINT IF NOT EXISTS.
SET @friend_index_exists := (
    SELECT COUNT(*) FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'Friend'
      AND index_name = 'uq_friend_user_pair');
SET @friend_index_sql := IF(
    @friend_index_exists = 0,
    'ALTER TABLE `Friend` ADD UNIQUE KEY `uq_friend_user_pair` (`user_id`,`friend_id`)',
    'SELECT 1');
PREPARE stage6_stmt FROM @friend_index_sql;
EXECUTE stage6_stmt;
DEALLOCATE PREPARE stage6_stmt;

SET @group_member_index_exists := (
    SELECT COUNT(*) FROM information_schema.statistics
    WHERE table_schema = DATABASE() AND table_name = 'GroupUser'
      AND index_name = 'uq_group_user_pair');
SET @group_member_index_sql := IF(
    @group_member_index_exists = 0,
    'ALTER TABLE `GroupUser` ADD UNIQUE KEY `uq_group_user_pair` (`group_id`,`user_id`)',
    'SELECT 1');
PREPARE stage6_stmt FROM @group_member_index_sql;
EXECUTE stage6_stmt;
DEALLOCATE PREPARE stage6_stmt;
