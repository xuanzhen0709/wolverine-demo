# run t0 stats
根据cs_ic_calculator模块计算出的IC值来自动化生成统计信息，一共会生成5张图片：  
1. 5min内平均IC
2. 按天连续的平均IC
3. 日内14401秒平均IC
4. 按日连续的因子相关系数（如果因子数量大于1）
5. 日内14401秒平均的因子相关系数（如果因子数量大于1）

## 如何使用
1. 需要先使用 run_cs_ic_calculator.py 模块生成指定截面因子的IC值，结果会保存在`{output}\cs_ic.{信号名称}.{开始日期}.{结束日期}.{future_bias}`文件夹中，且文件名为`{信号名称}.csv`
2. 使用 `python scripts/run_t0_stats.py -ic {信号IC文件保存的文件夹路径} -ic {信号IC文件保存的文件夹路径} -o {output_dir}  -s {start_date} -e {end_date}`来输出统计数据，例如：`python scripts/run_t0_stats.py -ic output/cs_ic.csstock-py1.20230701.20230709.5m -ic  output/cs_ic.csstock-py.20230701.20230709.5m -o output/ -s 20230701 -e 20230709`。程序会自动从文件夹名称中提取信息并检查IC数据，无误后将3张或者5张统计数据图片保存在指定的输出文件夹中。

## 注意
1. 如果统计天数较多，可以在`plt.figure()`函数内指定图片大小，以使图片更加直观。

