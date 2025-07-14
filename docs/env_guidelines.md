# env规范

配置文件env包含部分保留变量，用于特殊目的，因子使用过程中需要使用这些保留字段，方便系统在不同环境下自动运行。


## static_data_dir

这个目录用于存放因子静态数据，例如特定的numpy文件etc。生产和实盘环境会自动替换。


## prodcache_dir

prodcache_dir: str, 指定ProdCache数据目录，生产和实盘环境会自动替换。

prodcache_datasets: list, 指定ProdCache用到的子目录，例如AShareConcept等。系统依赖这个字段自动抓取数据依赖。


``` yaml
# wlsim.yml
env:
  prodcache_dir: /work/data/raw
  prodcache_datasets:
    - AShareConcept

# other config items
signal:
  config: {}
```

```c++

class Signal {
};

void Signal::initialize(const Config* root) {
}

// ....

void Signal::sod(const SodEvent* ev) {
    // load prodcache data
    {
      const auto ystd = wlcalendar_shift(ev->date, -1);
      stdfs::path data_path = stdfs::path(wlenv_get("prodcache_dir")) / "AShareConcept" / std::to_string(ystd);
      // read data_path
    }
    
}

```


## model_dir

model_dir用于指定因子的模型目录，生产和实盘会自动替换。该目录下需要按照交易日创建子文件夹，生产过程中it系统会自动获取当天的模型目录，确保因子可以读到的同时，不会误读历史或者未来版本。

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
