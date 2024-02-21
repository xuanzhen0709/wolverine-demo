- [搭建wlsim运行环境](#搭建wlsim运行环境)
  - [基础环境](#基础环境)
    - [自动安装](#自动安装)
    - [手动安装](#手动安装)
  - [运行时程序](#运行时程序)
- [构建本项目](#构建本项目)
- [文档和数据说明](#文档和数据说明)
  
# 搭建wlsim运行环境

wlsim环境目前包括基础环境（python/c++）以及运行时程序，安装运行时程序时，请确保基础环境已经安装完成并且启用。

```
enable_wlsim_env
```

<mark>wlsim框架版本要和wolverine-demo版本适配，一般使用最新日期的即可<mark>  
<mark>如果当前wolverine-demo版本使用满足需求，建议非必要不使用git pull更新</mark>  
<mark>如果使用git pull更新，可能需要重新安装运行时程序并且重新编译因子，以确保能正常使用</mark>  

## 基础环境

可以选择使用shell脚本一键搭建，或者手动搭建。  

### 自动安装
- git clone http://192.168.1.101:18086/nickchenyj/wolverine-demo.git
- cd wolverine-demo/scripts
- ./install.sh {wlsim框架版本}
- source ~/.profile

### 手动安装
<mark>不要使用conda环境！</mark>  
<mark>不要使用conda环境！！</mark>  
<mark>不要使用conda环境！！！</mark>  
- 添加环境变量  
    - 向 `~/.bashrc`文件中插入以下两行：
        ```
        export PATH=$PATH:~/.local/bin
        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/.local/lib
        ``` 
        ![add path](assets/add_path.png)
    - 执行 `source ~/.bashrc` 使其生效
    - 验证是否添加成功
        ```
        echo $PATH
        echo $LD_LIBRARY_PATH
        ```
        ![echo path](assets/echo_path.png)

- 设置编译环境
    - 查看Python和gcc版本
        ```
        gcc -v
        python3 -V # 或者执行 python3.8 -v
        ```
        gcc的版本需要是11，Python的版本需要为3.8，如果系统中gcc和Python版本不满足要求，需要升级版本
    - 设置系统python和gcc版本
        向 `~/.bashrc`文件中插入以下两行：
        ```
        source /opt/rh/devtoolset-11/enable
        source /opt/rh/rh-python38/enable
        ```
        ![enable python gcc](assets/enable_python_gcc.png)
    - 执行 `source ~/.bashrc` 使其生效
    - 验证是否设置成功  
        ![gcc python version](assets/gcc_python_version.png)

- 如果wlsim的编译环境和系统中已有框架冲突，可以使用以下方法避免冲突：
    - 临时性地激活devtoolset-11和rh-python38  
        ```
        source /opt/rh/devtoolset-11/enable
        source /opt/rh/rh-python38/enable
        ```
    - 为项目创建python虚拟环境
        ```
        python3 -m venv ~/venv/wlsim
        ```
    - 在 ~/.bashrc 中插入;
        ```
        export PATH=$PATH:~/.local/bin
        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/.local/lib
        function enable_wlsim_env()
        {
        local plat=$(uname -r)

        if [[ $plat =~ el8 ]]; then
            if shopt -q login_shell; then
                echo "el8, enabling gcc-toolset-11"
            fi
            source /opt/rh/gcc-toolset-11/enable
        elif [[ $plat =~ el7 ]]; then
            if shopt -q login_shell; then
                echo "el7, enabling devtoolset-11"
            fi
            source /opt/rh/devtoolset-11/enable

            if shopt -q login_shell; then
                echo "el7, enabling rh-python38"
            fi
            source /opt/rh/rh-python38/enable

            if shopt -q login_shell; then
                echo "el7, enabling wlsim python38 venv
            fi
            source ~/venv/wlsim/bin/activate
        fi
        }
        ```
        ![wrap](assets/wrap.png)
    - 执行 `source ~/.bashrc` 使其生效, 每次使用 wlsim 之前执行 `enable_wlsim_env` 来激活编译环境

- 安装必要的python包：      
        `python3.8 -m pip install --user Cython wheel "numpy>=1.23.4" pymssql sqlalchemy pandas matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple`


## 运行时程序

包括wlsim（主框架）和wlmd（行情模块）两部分，两部分独立更新但wlmd又对wlsim存在版本依赖。安装过程中需要分别安装两个包。
安装包位于：
  - /mnt/nas-3/homes/nickchenyj/packages/wlsim(及wlmd)(正式员工)  
  - /mnt/nas-i/homes/nickchenyj/packages/wlsim(及wlmd) (实习生)  

wlsim目录下会以多级目录的形式通过commitID和系统版本区分安装包，这里可根据需求安装相应的版本（集群环境系统版本为el7.py38）。

或者在安装包目录下运行
```
  ls -lrt
```
查看不同版本的更新日期，进而安装最新版。

wlmd目录下额外有一级目录用来区分wlsim版本，安装时请注意选择。


# 构建本项目
  - <mark>**实习生**请使用 `intern` 分支</mark>：`git clone http://192.168.1.101:18086/nickchenyj/wolverine-demo.git -b intern`
    
    在`wolverine-demo`项目的根路径下执行以下语句：
    ```
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
- 测试用例  
    在`wolverine-demo`项目的根路径下执行：`wl-sim src/csstock/wlsim.yml
`
- **注意**，在wlsim的后续使用过程中：
    - wlsim回测框架更新时，需要重新执行`python3 install_runtime.py`来更新回测系统
    - 使新的因子实现生效，比如`wolverine-demo/src/xxxxxx/`中的`CMakeLists.txt`，`*.cpp`，`*.py`，`*.pyx`有改动，或者`git pull`之后，需要执行
        ```
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

# 文档和数据说明
- [wlsim tutorial](tutorial.md)
