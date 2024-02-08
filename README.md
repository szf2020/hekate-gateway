# hekate
Outdoor Lora Gateway with Pico Pi

# submodules
git submodule add "https://github.com/lorabasics/basicstation.git" external/basicstation



# prerequisites
git submodule init 
git submodule update

~~~
cd external/basicstation/examples/simulation
pip install -r requirements.txt
make station
~~~


# custom gateway

## to use vs code for debugging
* open wsl terminal
~~~
code .
~~~

## build
~~~
cd custom-gateway
cmake -B build
cmake --build build
~~~



## basicstation

What is part of platform layer (sys)?

src-linux/sys_linux #main
src/sys.c #socket options
src/lgwsim.c #socket write

sys_log.c #thread for logging

build-linux-testsim #header file is generated


Radio Layer (RAL)



# Troubleshooting


~~~
./prep.sh: line 2: $'\r': command not found
~~~

Solution:
~~~
sudo apt install dos2unix
dos2unix external/basicstation/examples/cups/prep.sh
dos2unix external/basicstation/deps/lgw/prep.sh
dos2unix external/basicstation/deps/mbedtls/prep.sh
~~~



# MicroPython
install prerequisites
~~~
sudo apt-get install build-essential libffi-dev git pkg-config
~~~
~~~
cd external/micropython/ports/unix
make submodules
make
~~~

~~~
make USER_C_MODULES=../../../../mpy-modules/
~~~