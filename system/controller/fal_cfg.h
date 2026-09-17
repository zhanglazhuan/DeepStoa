// system/controller/fal_cfg.h
// FAL configuration — flash devices + partition table
//
// 两块独立的 FlashDB 区域，对应 partitions.csv 里的两个 esp_partition：
//   fdb_sys   0x8000  (32K)  厂家数据：机器码、序列号、出厂标定。
//                            产线单独刷入，恢复出厂设置**不擦**。
//   fdb_user  0x20000 (128K) 用户数据：Wi-Fi 凭据、各 app 的设置和使用记录。
//                            恢复出厂设置擦掉这一块。
//
// 之所以做成两个 esp_partition 而不是一块里划两段：产线要能用
// esptool / parttool 单独刷 sys，而用户侧的擦除也只要整块擦，不用算偏移。

#ifndef FAL_CFG_H
#define FAL_CFG_H

#define NOR_FLASH_SYS_NAME   "norflash_sys"
#define NOR_FLASH_USER_NAME  "norflash_user"

extern const struct fal_flash_dev nor_flash_sys;
extern const struct fal_flash_dev nor_flash_user;

#define FAL_FLASH_DEV_TABLE  \
{                             \
    &nor_flash_sys,           \
    &nor_flash_user,          \
}

#define FAL_PART_TABLE                                              \
{                                                                   \
    {FAL_PART_MAGIC_WORD, "fdb_sys",  NOR_FLASH_SYS_NAME,           \
     0, 32 * 1024, 0},                                              \
    {FAL_PART_MAGIC_WORD, "fdb_user", NOR_FLASH_USER_NAME,          \
     0, 128 * 1024, 0},                                             \
}

#define FAL_PART_HAS_TABLE_CFG

#endif
