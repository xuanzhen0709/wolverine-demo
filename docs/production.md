# 生产提交流程

## 联系@Yanjun获取对应的git repo

## 在自己的dev分支上开发新因子

## 确认因子可以在最新的金刚狼版本上编译，运行

## 在离线测试机器上测试因子，确保全天结果符合预期

   参考仓库<http://git.cfi:18086/duanlian/wlf_replay_deploy>

## 在dev分支上准备因子配置文件

配置文件统一位于configs/目录下

- 截面类因子: `configs/${target}/${user}.${name}.yml`

  例如configs/stocks.CHN/nickchenyj.fct1.yml

- 时序类因子: `configs/${dataset}/${target}/${user}.${name}.yml`

  例如configs/binance/BTCUSDT.BIANUM/nickchenyj.fct1.yml
