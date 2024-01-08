# wlsim
wlsim是面向中高频股票和期货的回测系统，目前可以实现因子的计算和评价因子质量。
- 提供流式数据接口，因子根据需求自行维护历史数据。
- 支持使用Python和C++编写因子。
- 使用yaml作为配置文件语言，通过配置文件调节运行参数。
- 数据模块：包括基本数据(reference data)和实时行情数据。行情数据分为时序和截面两大类。
  -   时序（期货）：快照、bar数据（利用快照数据实时生成）
  -   截面（股票）：3s快照、逐笔。
- 支持使用ic值来评价因子质量

## 介绍
- Quick Start
  - [English version](docs/quick_start.md)
  - [中文版](docs/quick_start_CN.md)  
- [wlsim tutorial](docs/tutorial.md)

## 常见QA
[Q&A] (docs/QA.md)