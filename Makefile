all: capture_camera

capture_camera: capture_camera.c
	gcc -o $@ $< -Wall -lm -ltiff

clean:
	rm -f capture_camera

