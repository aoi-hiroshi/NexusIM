-- Fresh, container-only NexusIM schema. The official MySQL entrypoint runs
-- this file only when the named data volume is initialized for the first time.
-- It never reads or changes the Ubuntu host's existing chat/nexusim database.
-- The official image executes this file with MYSQL_DATABASE selected.

CREATE TABLE IF NOT EXISTS `User` (
    `id` INT NOT NULL AUTO_INCREMENT,
    `name` VARCHAR(64) NOT NULL,
    `password` VARCHAR(255) NOT NULL,
    `state` VARCHAR(20) NOT NULL DEFAULT 'offline',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `Friend` (
    `user_id` INT NOT NULL,
    `friend_id` INT NOT NULL,
    UNIQUE KEY `uq_friend_user_pair` (`user_id`, `friend_id`),
    KEY `idx_friend_friend_id` (`friend_id`),
    CONSTRAINT `fk_friend_user`
        FOREIGN KEY (`user_id`) REFERENCES `User` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_friend`
        FOREIGN KEY (`friend_id`) REFERENCES `User` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `AllGroup` (
    `id` INT NOT NULL AUTO_INCREMENT,
    `groupname` VARCHAR(100) NOT NULL,
    `groupdesc` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `GroupUser` (
    `group_id` INT NOT NULL,
    `user_id` INT NOT NULL,
    `grouprole` VARCHAR(20) NOT NULL DEFAULT 'normal',
    UNIQUE KEY `uq_group_user_pair` (`group_id`, `user_id`),
    KEY `idx_group_user_user_id` (`user_id`),
    CONSTRAINT `fk_group_user_group`
        FOREIGN KEY (`group_id`) REFERENCES `AllGroup` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_group_user_user`
        FOREIGN KEY (`user_id`) REFERENCES `User` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `OfflineMessage` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `user_id` INT NOT NULL,
    `message` MEDIUMTEXT NOT NULL,
    PRIMARY KEY (`id`),
    KEY `idx_offline_message_user_id` (`user_id`),
    CONSTRAINT `fk_offline_message_user`
        FOREIGN KEY (`user_id`) REFERENCES `User` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
