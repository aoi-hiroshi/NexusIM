-- Run once before starting the Stage 1 server. Existing short password columns
-- cannot hold the versioned PBKDF2-SHA256 representation.
ALTER TABLE `User`
    MODIFY COLUMN `password` VARCHAR(255) NOT NULL;

ALTER TABLE `User` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
ALTER TABLE `OfflineMessage` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
