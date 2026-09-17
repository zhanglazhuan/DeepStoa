# fs_image —— 内部 flash FAT 分区的内容

这个目录会被 CMake 打成 FAT 镜像烧进 `storage` 分区，运行时挂载在 `/sdcard`。
往这里放文件 = 往设备的 `/sdcard` 放文件。改完执行 `idf.py flash` 即可。

```
fs_image/
  music/        → /sdcard/music，播放器扫描的固定目录
```

## 为什么需要它

板子上还没有 SD 卡座，但 **ID3 元数据解析、VBR 时长、seek 索引表这三件全是纯
文件格式解析，不需要 codec，只需要有真实文件可读**。这个分区就是用来解掉
"有文件可读"这个前提的。

## 现在的内容

两个程序生成的 WAV（正弦音，3 秒 / 7 秒）。WAV 头里有精确的采样率和数据长度，
所以能用来验证"从文件真实解析时长"这条路 —— 进度条的总长必须显示 00:03 和 00:07。

## 要验证 MP3 的 ID3 / VBR

把你自己的 `.mp3` 拷进 `music/`，重新 `idf.py flash`。
分区 2MB，注意别放超。放不下就调大 `partitions.csv` 里 `storage` 的 size
（flash 16MB，`0x840000` 之后还有约 8MB 空闲）。

> 改分区表会挪动 `fdb_kvdb`，设备上的 FlashDB 数据会失效，
> 需要 `idf.py erase-flash flash`。
