# wlsim

wlsim是面向中高频股票和期货的回测系统。

- 提供流式数据接口，因子根据需求自行维护历史数据。

- 支持使用Python和C++编写因子。

- 使用yaml作为配置文件语言，通过配置文件调节运行参数。

- 数据模块：包括基本数据(reference data)和实时行情数据。行情数据分为时序和截面两大类。

  - 时序：快照

  - 截面：快照、逐笔。

- 支持使用ic值来评价因子质量

## 使用文档

- [Quick Start](docs/quick_start.md)  

- [ProdCache数据使用规范](docs/prodcache_guidelines.md)

- [因子库生产提交手册](docs/production.md)

- [Q&A](docs/QA.md)

## 常见QA
