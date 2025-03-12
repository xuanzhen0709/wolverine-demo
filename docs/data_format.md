# Data Format

## 时序快照格式

可以使用金刚狼提供的包读取时序快照。

安装好金刚狼wlsim主包以后，用以下方式读取时序快照

```

from cfi.wolverine.misc.snapshot_reader import SnapshotReader

reader = SnapshotReader("path to data file")
df = reader.read_csv(level_nr=5)

```

也可以安装wlmd程序后，使用wlmddump-snapshot程序dump出数据，具体使用方式可以参照--help文档。
