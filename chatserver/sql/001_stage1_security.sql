-- Run once before starting the Stage 1 server. Existing short password columns
-- cannot hold the versioned PBKDF2-SHA256 representation.
ALTER TABLE `user`
    MODIFY COLUMN `password` VARCHAR(255) NOT NULL;

ALTER TABLE `user` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
ALTER TABLE `offlinemessage` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
