#!/bin/bash
set -e
function disable_conda(){
    conda deactivate | echo "no conda"
}

function create_python_venv(){
    python_38=${1}
    venv_path=${2}
    source ${python_38}
    python3 -m venv ${venv_path}
}

function edit_bashrc(){
    gcc_11=${1}
    python_38=${2}
    python_venv=${3}

    bashrc_file="${HOME}/.bashrc"
    
    if [ ! -f ${bashrc_file} ]; then
        touch ${bashrc_file}
    fi

    cat>>"${bashrc_file}"<<-EOF
export PATH=\$PATH:~/.local/bin
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:~/.local/lib

function enable_wlsim_env()
{
local plat=\$(uname -r)

if [[ \$plat =~ el8 ]]; then
    if shopt -q login_shell; then
        echo "el8, enabling gcc-toolset-11"
    fi
    source ${gcc_11}
elif [[ \$plat =~ el7 ]]; then
    if shopt -q login_shell; then
        echo "el7, enabling devtoolset-11"
    fi
    source ${gcc_11}

    if shopt -q login_shell; then
        echo "el7, enabling rh-python38"
    fi
    source ${python_38}

    if shopt -q login_shell; then
        echo "el7, enabling wlsim python38 venv"
    fi
    source ${3}/bin/activate
fi
}           
EOF
}

function install_python_pkg(){
    python_venv=${1}
    ${python_venv}/bin/pip3 install --upgrade pip  -i https://pypi.tuna.tsinghua.edu.cn/simple 
    ${python_venv}/bin/pip3 install  Cython wheel "numpy>=1.23.4" pymssql sqlalchemy pandas matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple
}


function setup_wlsim(){
    if [ -d "/mnt/nas-3/homes/nickchenyj/packages/wlsim" ]; then
        pkg_folder="/mnt/nas-3/homes/nickchenyj/packages/wlsim"
    elif [ -d "/mnt/nas-i/homes/nickchenyj/wlsim/packages" ]; then
        pkg_folder="/mnt/nas-i/homes/nickchenyj/wlsim/packages"
    else
        echo "cannot find wlsim/packages" 1>&2
        exit 1
    fi
    python_venv=${1}
    wlsim_version=${2}
    ${python_venv}/bin/python3 ${pkg_folder}/${wlsim_version}/el7.py38/install_runtime.py
}

function edit_profile(){
    profile_file="${HOME}/.profile"
    
    if [ ! -f ${profile_file} ]; then
        touch ${profile_file}
    fi

    cat>>"${profile_file}"<<-EOF
if [ -n "${BASH_VERSION}" ]; then
  if [ -f "${HOME}/.bashrc" ]; then
      source "${HOME}/.bashrc"
  fi
fi        
EOF
}

if [ $# -ne 1 ]; then
    echo "Please enter the wlsim version:"
    echo "- If you are a regular employee, you can view it in the following path:"
    echo "      /mnt/nas-3/homes/nickchenyj/packages/wlsim/"
    echo "- If you are an intern, you can view it in the following path:"
    echo "      /mnt/nas-i/homes/nickchenyj/wlsim/packages/"
    exit 1
fi
wlsim_version=$1

venv_path="${HOME}/venv/wlsim"
echo python venv: ${venv_path}
if [ -f "/opt/rh/gcc-toolset-11/enable" ]; then
    gcc_11="/opt/rh/gcc-toolset-11/enable"
else
    gcc_11="/opt/rh/devtoolset-11/enable"
fi
python_38="/opt/rh/rh-python38/enable"
echo "gcc 11: ${gcc_11}"
echo "python 3.8: ${python_38}"
disable_conda
create_python_venv ${python_38} ${venv_path}
edit_bashrc  ${gcc_11} ${python_38} ${venv_path}
edit_profile
setup_wlsim ${venv_path} ${wlsim_version}
install_python_pkg ${venv_path}