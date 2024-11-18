# 规范

## 配置文件特殊env变量

配置文件中的以下env变量为保留变量，需要因子遵守，即无特殊情况，相关的数据目录都需要通过这些字段生成，方便测试和实盘环境快速切换。

* prodcache_dir
  用于指定prodcache目录

* model_dir
  用于指定model存放目录

## 因子读取第三方数据

当因子需要读取第三方数据时，需要采取以下模式

* 默认读取env/prodcache_dir，代码中以此为根目录合成数据路径

* 因子自身配置提供override配置(例如 data_dir: /xxx/yyy/zzz/)，然后代码中以此为基础合成数据路径

```c++

class Signal {
    // ...
    stdfs::path m_data_dir;
};

void Signal::initialize(const Config* root) {
    // ...
    // 获取override, 若不存在则存空字符串
    m_datadir = (*root)["data_dir"].as<std::string>("");
}

// ....

void Signal::sod(const SodEvent* ev) {
    //....
    const auto ystd = wlcalendar_shift(ev->date, -1);
    // ...
    stdfs::path data_path;
    if (m_data_dir.empty()) {
        // 如果没有定义override, 使用系统prodcache_dir合成路径
        data_path = stdfs::path(wlenv_get("prodcache_dir")) / "some_subdir" / std::to_string(ystd);
    } else {
        // 用override路径合成
        data_path = m_data_dir / "any_path" / std::to_string(ystd);
    }
    // read data_path
}

```
