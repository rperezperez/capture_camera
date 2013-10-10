capture_camera
==============

Captura de imagenes de cámaras USB - Tiff sin compresión

Preinstalación
=========================================================================

sudo apt-get update
sudo apt-get install intltool autotools-dev libsdl1.2-dev libgtk-3-dev portaudio19-dev libpng12-dev libavcodec-dev libavutil-dev libv4l-dev libudev-dev libpulse-dev

sudo apt-get install libtiff-dev

Compilar
=========================================================================

make

Uso
=========================================================================

./capture_camera --help

Apéndices
=========================================================================

** La imagen se captura según el formato:

http://linuxtv.org/downloads/v4l-dvb-apis/V4L2-PIX-FMT-YUYV.html

** El uso del tiempo de exposición según: 

http://linuxtv.org/downloads/v4l-dvb-apis/extended-controls.html

** generación de tiff

http://research.cs.wisc.edu/graphics/Courses/638-f1999/libtiff_tutorial.htm


