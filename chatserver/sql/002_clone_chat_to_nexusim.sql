-- Non-destructive one-time clone for NexusIM.
-- Source schema `chat` is read only; it is never altered or deleted here.
-- Run with an account that can CREATE DATABASE/TABLE and SELECT from `chat`.

CREATE DATABASE IF NOT EXISTS `nexusim`
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `nexusim`.`User` LIKE `chat`.`User`;
CREATE TABLE IF NOT EXISTS `nexusim`.`Friend` LIKE `chat`.`Friend`;
CREATE TABLE IF NOT EXISTS `nexusim`.`AllGroup` LIKE `chat`.`AllGroup`;
CREATE TABLE IF NOT EXISTS `nexusim`.`GroupUser` LIKE `chat`.`GroupUser`;
CREATE TABLE IF NOT EXISTS `nexusim`.`OfflineMessage` LIKE `chat`.`OfflineMessage`;

ALTER TABLE `nexusim`.`User`
    MODIFY COLUMN `password` VARCHAR(255) NOT NULL;

ALTER TABLE `nexusim`.`User`
    CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
ALTER TABLE `nexusim`.`Friend`
    CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
ALTER TABLE `nexusim`.`AllGroup`
    CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
ALTER TABLE `nexusim`.`GroupUser`
    CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
ALTER TABLE `nexusim`.`OfflineMessage`
    CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

DROP PROCEDURE IF EXISTS `nexusim`.`clone_chat_data_once`;
DELIMITER //
CREATE PROCEDURE `nexusim`.`clone_chat_data_once`()
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    IF EXISTS (SELECT 1 FROM `nexusim`.`User` LIMIT 1)
       OR EXISTS (SELECT 1 FROM `nexusim`.`Friend` LIMIT 1)
       OR EXISTS (SELECT 1 FROM `nexusim`.`AllGroup` LIMIT 1)
       OR EXISTS (SELECT 1 FROM `nexusim`.`GroupUser` LIMIT 1)
       OR EXISTS (SELECT 1 FROM `nexusim`.`OfflineMessage` LIMIT 1) THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'nexusim target tables are not empty; clone was not run';
    END IF;

    START TRANSACTION;
    INSERT INTO `nexusim`.`User` (`id`, `name`, `password`, `state`)
        SELECT `id`, `name`, `password`, 'offline' FROM `chat`.`User`;
    INSERT INTO `nexusim`.`Friend` (`userid`, `friendid`)
        SELECT `userid`, `friendid` FROM `chat`.`Friend`;
    INSERT INTO `nexusim`.`AllGroup` (`id`, `groupname`, `groupdesc`)
        SELECT `id`, `groupname`, `groupdesc` FROM `chat`.`AllGroup`;
    INSERT INTO `nexusim`.`GroupUser` (`groupid`, `userid`, `grouprole`)
        SELECT `groupid`, `userid`, `grouprole` FROM `chat`.`GroupUser`;
    INSERT INTO `nexusim`.`OfflineMessage` (`userid`, `message`)
        SELECT `userid`, `message` FROM `chat`.`OfflineMessage`;
    COMMIT;
END//
DELIMITER ;

CALL `nexusim`.`clone_chat_data_once`();
DROP PROCEDURE `nexusim`.`clone_chat_data_once`;

SELECT 'chat.User' AS source_table, COUNT(*) AS row_count FROM `chat`.`User`
UNION ALL
SELECT 'nexusim.User', COUNT(*) FROM `nexusim`.`User`
UNION ALL
SELECT 'chat.Friend', COUNT(*) FROM `chat`.`Friend`
UNION ALL
SELECT 'nexusim.Friend', COUNT(*) FROM `nexusim`.`Friend`
UNION ALL
SELECT 'chat.AllGroup', COUNT(*) FROM `chat`.`AllGroup`
UNION ALL
SELECT 'nexusim.AllGroup', COUNT(*) FROM `nexusim`.`AllGroup`
UNION ALL
SELECT 'chat.GroupUser', COUNT(*) FROM `chat`.`GroupUser`
UNION ALL
SELECT 'nexusim.GroupUser', COUNT(*) FROM `nexusim`.`GroupUser`
UNION ALL
SELECT 'chat.OfflineMessage', COUNT(*) FROM `chat`.`OfflineMessage`
UNION ALL
SELECT 'nexusim.OfflineMessage', COUNT(*) FROM `nexusim`.`OfflineMessage`;

-- These copied legacy accounts still require one successful isolated login so
-- ChatServer can replace the old value with PBKDF2-SHA256.
SELECT `id`, `name` AS legacy_password_account
FROM `nexusim`.`User`
WHERE `password` NOT LIKE 'pbkdf2_sha256$%';
