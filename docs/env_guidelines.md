# env规范

配置文件env包含部分保留变量，用于特殊目的，因子使用过程中需要使用这些保留字段，方便系统在不同环境下自动运行。


## ProdCache目录

prodcache_dir用于配置ProdCache数据目录。

当因子需要使用ProdCache时，除了配置prodcache_dir，请在因子配置中添加subdir字段指定具体的数据项，方便生产系统自动判断数据依赖。例如

``` yaml
# wlsim.yml
env:
  prodcache_dir: /work/data/raw

# other config items
signal:
  config:
    subdir: AShareConcept
```

```c++

class Signal {
    // suppose signal needs to access AShareConcept data
    std::string m_subdir;
};

void Signal::initialize(const Config* root) {
    // retrieve the subdir name from config
    m_subdir = (*root)["subdir"].as<std::string>();
}

// ....

void Signal::sod(const SodEvent* ev) {
    // load prodcache data
    {
      const auto ystd = wlcalendar_shift(ev->date, -1);
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
    
}

```


## 模型目录

model_dir用于指定因子的模型目录。该目录下需要按照交易日创建子文件夹，生产过程中it系统会自动获取当天的模型目录，确保因子可以读到的同时，不会误读历史或者未来版本。

``` yaml
# wlsim.yml
env:
  model_dir: /home/xxx/wlsim_test/modela/

# other config items
```

```c++

void Signal::sod(const SodEvent* ev) {
    // load model
    {
      const auto model_file = stdfs::path(wlenv_get("model_dir")) / std::to_string(ev->date) / "mymodelname.txt";
      // use model
    }
}

```
