# ProdCache规范

当因子需要读取第三方数据(ProdCache)时，需要遵守以下规范。

## 特殊env变量

配置文件中的以下env变量为保留变量，用于制定数据目录，不得用于其他用途。

- prodcache_dir
  用于指定prodcache目录

- model_dir
  用于指定model存放目录

## 特殊因子配置变量

- subdir
  用于指定数据子目录（相对于prodcache_dir）, 生产提交时此选项会被用来检测数据依赖。
  可以参考以下推荐实现，方便离线测试时指定特殊目录。

## 推荐实现

```c++

class Signal {
    // ...
    std::string m_subdir;
};

void Signal::initialize(const Config* root) {
    // ...
    m_subdir = (*root)["subdir"].as<std::string>();
}

// ....

void Signal::sod(const SodEvent* ev) {
    //....
    const auto ystd = wlcalendar_shift(ev->date, -1);
    // ...
    stdfs::path data_path;
    if (!m_subdir.empty() && m_subdir[0] == '/') {
        // 如果subdir以'/'开始，则认为是数据的完整目录，从subdir对应的目录读取数据
        data_path = stdfs::path(m_subdir) / std::to_string(ystd);
    } else {
        // 默认情况，通过prodcache_dir + subdir获取数据目录
        data_path = stdfs::path(wlenv_get("prodcache_dir")) / m_subdir / std::to_string(ystd);
    }
    // read data_path
}

```
