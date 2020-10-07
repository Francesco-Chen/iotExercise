CREATE TABLE `sensors` (
  `id` int NOT NULL AUTO_INCREMENT,
  `wifirssi` int DEFAULT NULL,
  `temp` decimal(2,0) DEFAULT NULL,
  `humid` int DEFAULT NULL,
  `tilt` int DEFAULT NULL,
  `time` timestamp NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=303 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci