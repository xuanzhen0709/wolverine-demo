<!-- TOC -->
- [QuickStart](#quickstart)
  - [初始化python和c++环境](#初始化python和c环境)
    - [从v1.6.x及以前版本升级而来](#从v16x及以前版本升级而来)
    - [全新安装](#全新安装)
  - [更新cmake](#更新cmake)
  - [安装wlsim(主程序),wlmd(行情模块)和cfi-operators(算子库，可选)](#安装wlsim主程序wlmd行情模块和cfi-operators算子库可选)
  - [构建本项目](#构建本项目)
  - [运行demo](#运行demo)
<!-- TOC -->
# QuickStart

金刚狼包含两个组件，两个组件独立更新。

- wlsim: 主框架
- wlmd：行情模块

安装包位于
`/mnt/nas-3/homes/nickchenyj/packages/{wlsim|wlmd}`

该目录下会以多级目录的形式区分包的版本、依赖关系、以及支持的系统环境,金刚狼目前适配了公司三种常见的操作系统环境，安装时需要选择相应的版本。

注意

- conda可能对兼容性造成影响，因此建议完全清除conda后再安装金刚狼。
- 请关注任何命令的输出，如果有错误提示，不要继续！！！

## 初始化python和c++环境

初始化脚本位于系统环境目录(el7.py38、deb11.py39、etc)下的install_wlsim_env目录。此目录脚本在支持的环境中通用，所以可以任选一个系统环境目录进入。

### 从v1.6.x及以前版本升级而来

此时需要清理历史遗留的配置和文件，再重新安装环境。

请从`~/.bashrc`中删除以下两行：

``` bash
source /opt/rh/devtoolset-11/enable
source /opt/rh/rh-python38/enable
```

然后运行清理脚本。

```bash
./cleanup_legacy.sh
# 或者
# bash ./cleanup_legacy.sh
```

如果想复用以前的金刚狼环境，请先启用之前的金刚狼环境再执行全新安装。启用之前金刚狼环境的命令通常是

```bash
enable_wlsim_env
```

### 全新安装

使用一键初始化脚本完成安装，并按提示操作。

```bash
./install.sh
# 或者
# bash ./install.sh
```

如果想登陆自动启动venv，请在`~/.bashrc`末尾加入

```bash
enable_wlsim_env
```

或者可以在任何时候输入`enable_wlsim_env`来启用对应python和c++环境。

## 更新cmake

启用环境后，升级cmake到较新版
```bash
pip3 install -U 'cmake<4'
```

## 安装wlsim(主程序),wlmd(行情模块)和cfi-operators(算子库，可选)

确保金刚狼环境已启用，当前环境的名称可以通过以下命令获取。

```bash
echo $WLSIM_ENV_KEY
````

进入对应的安装目录，运行

```bash
python3 install_runtime.py
```

## 构建本项目

clone当前项目

``` bash
git clone http://192.168.1.101:18086/nickchenyj/wolverine-demo.git
```

在`wolverine-demo`项目的根路径下执行以下语句：

```bash
# 构建debug版本
mkdir -p build/Debug
pushd build/Debug
cmake -DCMAKE_BUILD_TYPE=Debug ../../
# or Release build
# mkdir -p build/Release
# pushd build/Release
# cmake -DCMAKE_BUILD_TYPE=Release ../../

make -j8 install
popd

```

## 运行demo

在demos目录下有多个示例，通过`wl-sim xxx.yml`运行。
