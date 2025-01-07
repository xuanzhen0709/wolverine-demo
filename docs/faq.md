#  FAQ

## 安装目录说明

创建wlsim python环境时，会建立以下目录或文件

- ~/.wlsimrc和~/.wlsim

  金刚狼环境的基础脚本, 可以忽略

- ~/.local/wlsim.${os}.py${pyver}

  金刚狼总安装目录。不同的系统安装目录不一致，用于避免集群上不同系统的机器安装到同一目录造成冲突。该目录包含bin/lib/venv三个子目录。其中

  * ~/.local/wlsim.${os}.py${pyver}/bin/wlsim.examples

    金刚狼配置文件和cmake范例，包括

      * 常用的因子cmake文件
      * wlsim主配置说明(main.yml)
      * 行情模块配置说明(md.*.yml)

  * ~/.local/wlsim.${os}.py${pyver}/venv

    金刚狼python venv，所有python包会安装到此目录。

注意：若安装金刚狼（install_runtime.py)时若基础环境没有正确初始化(没有enable_wlsim_env等),金刚狼文件可能会安装到不同目录。


## wlsim升级以及因子改动后出现问题或者改动没生效

wlsim升级以及因子有任何改动后，请重新编译安装因子。

## conda编译问题

安装过conda系列，并且在编译前执行过`conda deactive`, 但cmake时python解释器的路径查找仍然有误。
![conda error]("./assets/conda_error.png")

CMAKE添加编译选项`Python3_EXECUTABLE`来指定python解释器位置, eg

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DPython3_EXECUTABLE=$(which python3) ../../
```

## 在部分机器上使用cs-mbo数据报错

``` shell
[check permission][info] host:{»úÆ÷Ãû}.cfi
[check permission][fatal] failed to initialize cs-mbo loader for {user}
no analys
```

`s21.cfi`、`s22.cfi`、`s23.cfi`和`s24.cfi`上的cs-mbo数据访问受白名单限制，只有白名单内的用户可以访问使用。如遇此问题，可以尝试换一台机器访问或者和 @谌炎俊 申请使用权限。

## 因子crash如何处理

可以先编译Debug版本因子，再次运行可能有更详细输出，或者采用gdb debug。

```bash
cd build/Debug
cmake -DCMAKE_BUILD_TYPE=Debug ../../
make -j32 install
```