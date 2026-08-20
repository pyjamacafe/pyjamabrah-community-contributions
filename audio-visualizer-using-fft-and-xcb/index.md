---
# Change the Date below
date: "2025-10-28"

# Add the title of the Post here
title: "Building an Audio Visualizer in C using Fast Fourier Transform(FFT) and XCB"

# replace the "sample/cover.png" with path to a 16:9 image
# this image will be shown in SEO optimization and when
# you share the post on social media.
thumbnail: "/posts/audio-visualizer-using-fft-and-xcb/cover.png"

# Add your GitHub handle here
author: "rudrasama"

# Add tags as suitable for the topic
tags:
  - "Fast Fourier Transform"
  - "XCB"
  - "C"
  - "X11 Display"
  - "Programming"

# If it fits a certain category of topics, add that
categories:
  - "Programming"
  - "C"

# If this is a Series of posts, Add the name of the series
series:
  - ""

# Delete the line below when submitting for review
#draft: true
---
I made this project to understand how audio data can be analyzed and displayed visually. The idea was to take a WAV file, process its samples using Fast Fourier Transform (FFT), and show the frequency spectrum in a window using XCB (X11 protocol), all in C.

<!--more-->

Before we move on, it’s good to understand the core ideas behind this project.

## Fast Fourier Transform (FFT)
FFT is an algorithm used to find the frequency components of a signal. It basically converts a signal from time domain to frequency domain or the other way around.

The main idea is to break down a signal into smaller parts of different frequencies. Doing this directly is very slow, so FFT is used to make it faster. It works by splitting the big DFT calculation into smaller ones, which reduces the time from $O(n^2)$ to $O(n \ log \ n)$. This makes a huge difference when the data size is large.

### Understanding the Math Behind FFT
Mathematical formula for DFT is:
$$
\sum_{n=0}^{N-1} x[n] \cdot e^{\frac{-j2\pi kn}{N}}
$$
Here, $x$ is the sampled data, $n$ is the sample index, and $k$ is the frequency bin index.
A frequency bin is basically an array of frequencies that we get from the sampled data after applying the DFT.

For single frequency bin $X[k]$, we have N multiplication and N addition operations, and for all N frequency bins, we have total $N \times N$ operations.

Now suppose we have 64 data points in our sample, then total operations will be $64 \times 64 = 4096$ operations, And if we have 1024 data points, then total operations will be $1024 \times 1024 = 1048576$, which is too slow.

To make it faster, we use the Fast Fourier Transform (FFT).

In FFT, we divide sample data points into even and odd parts.
This gives us:
$$
\underbrace{\sum_{n=0}^{N/2-1} x[2n] \cdot e^{\frac{-j2\pi kn}{N/2}}}_{even} + \underbrace{\sum_{n=0}^{N/2-1} x[2n+1] \cdot e^{\frac{-j2\pi k(n+1)}{N/2}}}_{odd}
$$

Now, if we normalize the odd part of the equation, we get:
$$
\underbrace{\sum_{n=0}^{N/2-1} x[2n] \cdot e^{\frac{-j2\pi kn}{N/2}}}_{even}
+
e^{\frac{-j2\pi k}{N}} \cdot \underbrace{\sum_{n=0}^{N/2-1} x[2n+1] \cdot \ e^{\frac{-j2\pi k n}{N/2}}}_{odd}
$$

If we further simplify this equation and take out the summation and exponential terms, we are left with:
$$
e^{\frac{-j2\pi kn}{N/2}} \cdot \sum_{n=0}^{N/2-1}
\left[
x[2n] + e^{\frac{-j2\pi k}{N}} \cdot x[2n+1]
\right]
$$

Lets rewrite equation in this form:
$$
E_k + e^{\frac{-j2\pi k}{N}} \cdot O_k \ \ \ \ \text{for k} = 0, 1, 2,...,N/2-1
$$

This can compute frequency bins from $0 \to \frac{n}{2}-1$.

For $\frac{N}{2} \to N$, we can exploit periodicity of the complex exponential.

This --- $e^{\frac{-j2\pi kn}{N}}$ is actually our twiddle factor, if we go from $0 \to N$. But if we go from $N \to 0$, we can write it like this --- $e^{\frac{-j2\pi k(N-n)}{N}}$

Lets normalize this reverse twiddle factor:
$$
e^{\frac{-j2\pi kN}{N}} \cdot e^{\frac{-j2\pi k \cdot -n}{N}} \\
e^{-j2\pi k} \cdot e^{\frac{-j2\pi k \cdot -n}{N}} \\
1 \cdot e^{\frac{-j2\pi k\cdot-n}{N}}
$$

If we look carefully, after normalizing the equation, what we get is just the conjugate of our twiddle factor:
$$
e^{\frac{-j2\pi k\cdot-n}{N}} = (e^{\frac{-j2\pi kn}{N}})^*
$$

What does it mean? It basically means that frequencies from $\frac{N}{2}$ to $N$ in the frequency bin are just the conjugates of frequencies from $0$ to $\frac{N}{2}-1$.

$$
E_k - e^{\frac{-j2\pi k}{N}} \cdot O_k \ \ \ \ \text{for k} = N/2, N/2+1, N/2+2,...,N
$$

This reduces the total number of multiplication and addition operations because we can reuse the results from previous calculations (divide and conquer).

This is the **Cooley–Tukey FFT algorithm**. Now it’s time to implement this in C

### FFT Implementation in C
FFT involves complex equations and imaginary numbers, so we need the C library called complex.h. It’s a standard library introduced in the C99 standard.

The **Cooley–Tukey FFT algorithm** is implemented using recursion. So the first thing we need to do is define a recursion function and base case for it.

```C
void _fft(double complex *x, unsigned int N, double complex *result){
    if(N == 1){
        // Return the only element left in sampled data as result.
        result[0] = (double complex)x[0];
        return;
    }
}
```
1. `double complex *x` - pointer to dynamically allocated memory that stores the sampled data points.
1. `unsigned int N` - represents the total number of samples in x.
1. `double complex *result` - pointer to dynamically allocated memory that stores the computed frequency bins.

```C

// Spliting Sample into Even and Odd.
// Allocate separate memory blocks for both even and odd components.
double complex *sub_even = (double complex *)malloc(sizeof(double complex) * (N / 2));
double complex *sub_odd = (double complex *)malloc(sizeof(double complex) * (N / 2));

// Copy data from the original input into even and odd memory blocks.
for (unsigned int i = 0; i < N / 2; i++) {
	sub_even[i] = x[2 * i];
	sub_odd[i] = x[2 * i + 1];
}

// Allocating memory blocks to store result for sub_even and sub_odd parts.
double complex *even = (double complex *)malloc(sizeof(double complex) * (N / 2));
double complex *odd = (double complex *)malloc(sizeof(double complex) * (N / 2));

```
1. `sub_even` and `sub_odd` stores even and odd data points of input sample.
1. `even` and `odd` stores even and odd the frequency bins computed from the even and odd parts during recursion..

```C
// Calling recursion
_fft(sub_even, N / 2, even);
_fft(sub_odd, N / 2, odd);
```

```C
// Computing Frequeny bins
for (unsigned int k = 0; k < N / 2; k++) {
	double complex twiddle = cexp(-2 * I * PI * k / N) * odd[k];
	result[k] = even[k] + twiddle;
	result[k + N / 2] = even[k] - twiddle;
}
```
1. `tiwddle` - The twiddle factor mentioned in the mathematical section.
1. `result[k]` - Stores the lower half of the frequency bins.
1. `result[k+ N/2]` - Stores the upper half of the frequency bins, which are the conjugates of the lower half.

```C
// Freeing memory which we allocated earlier.
free(sub_even);
free(sub_odd);
free(even);
free(odd);
```

whole code should look like this:
```C
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

void _fft(double complex *x, unsigned int N, double complex *result) {
	if (N == 1) {
		result[0] = (double complex)x[0];
		return;
	}

	double complex *sub_even =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));
	double complex *sub_odd =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));

	for (unsigned int i = 0; i < N / 2; i++) {
		sub_even[i] = x[2 * i];
		sub_odd[i] = x[2 * i + 1];
	}

	double complex *even =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));
	double complex *odd =
	    (double complex *)malloc(sizeof(double complex) * (N / 2));
	_fft(sub_even, N / 2, even);
	_fft(sub_odd, N / 2, odd);

	for (unsigned int k = 0; k < N / 2; k++) {
		double complex twiddle = cexp(-2 * I * PI * k / N) * odd[k];
		result[k] = even[k] + twiddle;
		result[k + N / 2] = even[k] - twiddle;
	}

	free(sub_even);
	free(sub_odd);
	free(even);
	free(odd);
}
```

Now we can call our FFT function like this:
```C
// Size of result is equal to number of elements we have in our input sample data.
double complex *result = (double complex *)malloc(sizeof(double complex) * N);
_fft(x, N, result);
```

## XCB
XCB (X C Binding) is an API for communicating with the X server on Linux. It is used to draw graphics, create windows, handle events, and do other GUI stuff directly from C programs. XCB is faster and more lightweight than the older Xlib library, and it gives more control over how you interact with the X server.

We use the XCB API to create a window on Linux where we can draw the visual representation of audio frequencies in real time.

### Pixmap in XCB
A Pixmap in XCB is like an off-screen image or a canvas where you can draw graphics before showing them on the window. You can draw pixels, lines, or shapes on a pixmap and then copy it to the window. This is useful for reducing flickering and making smooth graphics, because you prepare everything in the pixmap first and then display it at once.
A pixmap works like a grid where each cell represents a pixel. Each pixel stores RGB values, so by changing the color of each cell, we can draw images or graphics on the pixmap before showing it on the window.
Pixmap uses buffer of type `uint8_t` which stores each pixel. Size of buffer is dependent on width and height of canvas we want to display.
For example resolution of our canvas is 600x600. Width is 600 and height is 600.
- Then bytes per row will be $600 * (bpp / 8)$
    - bpp is bits per pixel, which means how many bits required to show single pixel. Normaly it is 1 bit for Monochrome bitmap, 8 bits for Grayscale bitmap, 24 bits for color bitmap and 32 bits for color bitmap with alpha (transparency). In our case it is 24.
- Total bytes (size of buffer) will be bytes per row times height, which is $1800 * 600 = 1080000$

We can access each pixel from pixmap buffer using this:
```
offset = bytes_per_pixel * x + bytes_per_row * y
```
1. `bytes_per_pixel` is `bpp / 8`.
1. `x` is column index and `y` is row index in pixmap grid.

## Parsing WAV File
A WAV file is a type of audio file that stores sound in an uncompressed format. It contains raw audio data, usually in PCM (Pulse Code Modulation), along with some headers that tell the computer about sample rate, number of channels, and bit depth. Since it’s uncompressed, WAV files are large but give exact audio quality without any loss.

I implmented my own WAV parsing logic, you can find it in my github repo or you can learn about WAV files and PCM [here](https://pyjamacafe.com/posts/pcm/).

## Coding Audio Visualizer

Whole code is splitted into different sections for better understanding.

### Headers to include.
```C
#include <alsa/asoundlib.h>     // Library to play audio in Linux system using C
#include <xcb/xcb.h>            // XCB API library for GUI
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include "libs/fft/fft.h"
#include "libs/stb_image.h"
#include "libs/wav_parser/wav_parser.h"
```

### Defining all global variables used in program.
```C
// HEIGHT and WIDTH used by XCB to draw window.
static const int WIDTH = 600;
static const int HEIGHT = 600;

// Sample size for FFT
static const uint32_t sample_size = 1024;
static const uint16_t smooth_factor = 12;

// Bars to show on Audio Visualizer.
static const uint16_t total_bars = sample_size / smooth_factor;
static const uint16_t bar_width = 4;
static const uint16_t gap = 2;

// 'interval' defines the time frame (in milliseconds) used to pick samples for each frame.
static const uint32_t interval = 23;  // Milliseconds

// Device to use to play audio.
static const char *device = "default";

// GIF image threshold
static const uint8_t gray_threshold = 100;
static const uint8_t invert_image = 0x00;

// Used for parallely computing frequencies and displaying on window.
atomic_int worker_thread_running = 1;
atomic_int need_pixmap_update = 0;

// Maximum amplitude in whole audio sample.
// Used for fitting bar on window perfectly with respect to frequency.
static uint64_t MAX_AMP = 1;
```
### Structs defined in program
Pixmap Struct - Used for storing information about Pixmap.
```C
typedef struct {
	uint8_t bpp;                // Bits per pixel
	uint8_t *buffer;            // Buffer to store pixel data
	uint8_t bytes_per_pixel;    //
	uint32_t bytes_per_row;     //
	uint32_t total_bytes;       // Total size of buffer in bytes
} Pixmap;
```

Bars Struct - Used for storing information about each bar.
```C
typedef struct {
	uint16_t length;            // Length of each bar. It changes dynamically.
	uint16_t position;          // Y axis position of each bar.
	uint8_t rev;                //
} Bar;
```

GIF Struct - Used for storing GIF information.
```C
typedef struct {
	int W;                      // WIDTH of GIF image
	int H;                      // HEIGHT of GIF image
	int frames;                 // Number of frames in GIF
	int comp;                   // Number of Components in GIF (3 for RGB).
	int delay;                  // Delay between two frames.
	uint8_t *buffer;            // Store data of all frames.
} GIF;
```

Argument Struct - Used for passing arguments to worker thread.
```C
typedef struct {
	uint16_t *mono_buffer;
	Pixmap *pixmap;
	Bar *bar;
	WAV *wav;
	GIF *gif;
	uint32_t audio_frames_in_time;
	uint32_t *offset;
} Arg_Struct;
```

### RGB to Grayscale.
This function is used for converting RGB GIF frame into grayscale.
```C
uint8_t rgb_to_grayscale(uint8_t R, uint8_t G, uint8_t B) {
	return (uint8_t)(0.299 * R + 0.587 * G + 0.114 * B);
}
```

### Loading GIF
This function loads GIF into program using its path and filename.
```C
int load_gif(const char *filename, GIF *gif) {
	FILE *file;
	stbi__context s;

	if (!(file = stbi__fopen(filename, "rb")))
		printf("can't fopen", "Unable to open file");
	stbi__start_file(&s, file);

	int x = 0;
	int y = 0;
	int z = 0;
	int comp = 0;
	int *delay = 0;

    // This will load all variables defined above and fill buffer with GIF pixel data
	uint8_t *buffer = stbi__load_gif_main(&s, &delay, &x, &y, &z, &comp, 4);

	uint32_t total_bytes = x * comp * y * z;

	if (buffer == NULL) {
		printf("Could not read GIF file");
		fclose(file);
		return 0;
	}

	if (x > WIDTH || y > HEIGHT) {
		printf(
		    "Width and Height of GIF can't be more than WINDOW's Width and "
		    "Height'");
		return 0;
	}

    // Intializing GIF struct
	gif->W = x;
	gif->H = y;
	gif->frames = z;
	gif->comp = comp;
	gif->delay = *delay;
	gif->buffer = (uint8_t *)malloc(sizeof(uint8_t) * total_bytes);

	if (gif->buffer == NULL) return 0;

    // Copying GIF pixel data to struct
	memcpy(gif->buffer, buffer, total_bytes);

	free(buffer);
	free(delay);
	fclose(file);

	return 1;
}
```

### Drawing GIF Frame
GIF has multiple frames and this function draw those frames on display one by one.
```C
void draw_image(Pixmap *pixmap, GIF *gif, uint8_t frame) {
    // Origin position of Image on window
	uint16_t start_x = (WIDTH / 2) - (gif->W / 2);
	uint16_t start_y = 50;

    // If last frame, start from first
	if (frame >= gif->frames) frame = 0;

    // Total bytes to store single frame
	uint32_t total_frame_bytes = gif->W * gif->comp * gif->H;

	for (uint16_t y = 0; y < HEIGHT; y++) {
		for (uint16_t x = 0; x < WIDTH; x++) {
            // Position of pixel inside pixmap
			uint32_t pixmap_offset =
			    x * pixmap->bytes_per_pixel + y * pixmap->bytes_per_row;

            // Make sure the pixel we are modifying is within image boundaries
			if ((x >= start_x && x < (gif->W + start_x)) &&
			    (y >= start_y && y < (gif->H + start_y))) {

                // Position of pixel inside GIF image buffer
				uint32_t gif_offset = (x - start_x) * gif->comp +
				                      (y - start_y) * gif->W * gif->comp;

				uint8_t R =
				    (total_frame_bytes * frame + gif->buffer)[gif_offset + 0];
				uint8_t G =
				    (total_frame_bytes * frame + gif->buffer)[gif_offset + 1];
				uint8_t B =
				    (total_frame_bytes * frame + gif->buffer)[gif_offset + 2];

				uint8_t gray = rgb_to_grayscale(R, G, B);

				if (gray > gray_threshold) {
                    // This allow us to invert white and black pixel by toggling invert_image variable
					pixmap->buffer[pixmap_offset + 0] = 0xFF ^ invert_image;
					pixmap->buffer[pixmap_offset + 1] = 0xFF ^ invert_image;
					pixmap->buffer[pixmap_offset + 2] = 0xFF ^ invert_image;
				} else {
					pixmap->buffer[pixmap_offset + 0] = 0x00 | invert_image;
					pixmap->buffer[pixmap_offset + 1] = 0x00 | invert_image;
					pixmap->buffer[pixmap_offset + 2] = 0x00 | invert_image;
				}
			} else {
				pixmap->buffer[pixmap_offset + 0] = 0x00;
				pixmap->buffer[pixmap_offset + 1] = 0x00;
				pixmap->buffer[pixmap_offset + 2] = 0x00;
			}
		}
	}
}
```

### Clearing whole pixmap
Before drawing new bar or GIF image, we need to clear whole pixmap.
```C
void clear_pixmap(Pixmap *pixmap) {
	for (uint16_t y = 0; y < HEIGHT; y++) {
		for (uint16_t x = 0; x < WIDTH; x++) {
			uint32_t offset =
			    pixmap->bytes_per_pixel * x + pixmap->bytes_per_row * y;
			pixmap->buffer[offset + 0] = 0x00;
			pixmap->buffer[offset + 1] = 0x00;
			pixmap->buffer[offset + 2] = 0x00;
		}
	}
}
```

### Creating bar
This is used for creating single bar on pixmap.
```C
void create_bar(Pixmap *pixmap, uint16_t start, uint16_t width, uint16_t length,
                uint32_t color) {
    // Extracting RGB components from color vairable
	uint8_t R = 0, G = 0, B = 0;
	B = B | (color >> 16);
	G = G | (color >> 8);
	R = R | color;

    // Drawing vertical bar on pixmap
	for (uint16_t y = HEIGHT - length; y < HEIGHT; y++) {
		for (uint16_t x = start; x <= start + width; x++) {
			uint32_t offset =
			    pixmap->bytes_per_pixel * x + pixmap->bytes_per_row * y;
			pixmap->buffer[offset + 0] = B;
			pixmap->buffer[offset + 1] = G;
			pixmap->buffer[offset + 2] = R;
		}
	}
}
```

### Extracting frequencies from sample audio.
This function extract frequencies from mono audio sameple and smooths it.

```C
uint64_t *get_bands(WAV *wav, uint16_t *buffer, uint32_t total_frames,
                    uint32_t offset) {
    // Samples to take for FFT
	uint32_t sample_size = 1024;
    // Frequency bin group size for averaging
	uint16_t smooth_factor = 12;

	double complex *samples =
	    (double complex *)malloc(sizeof(*samples) * sample_size);

	if (samples == NULL) return NULL;

	for (uint32_t i = 0; i < total_frames && i < sample_size; i++)
		samples[i] = (double complex)buffer[offset + i];

    // If we are short of sample data, them make remaning samples 0
	for (uint32_t i = total_frames; i < sample_size; i++) samples[i] = 0;

	double complex *bin = fft(samples, sample_size);

	if (bin == NULL) return NULL;

    // Output is half the sample size since frequencies above N/2 are conjugates of the lower half
	uint64_t *output =
	    (uint64_t *)malloc(sizeof(*output) * (sample_size / 2 / smooth_factor));

	if (output == NULL) return NULL;

	uint32_t output_index = 0;
	uint32_t bin_index = 0;
	while (bin_index < sample_size / 2) {
		uint64_t avg_mag = 0;

        // First 4 frequencies represent bass range, so they are not smoothed
		if (bin_index < 4) {
			bin_index++;
			continue;
		}

		for (uint32_t j = 0; j < smooth_factor; j++) {
            // Computing strength of frequency
			uint64_t magnitude = (uint64_t)cabs(bin[bin_index]);
			avg_mag = avg_mag + magnitude;
			bin_index++;
		}

		output[output_index] = avg_mag / smooth_factor;
		if (output[output_index] > MAX_AMP) MAX_AMP = output[output_index];
		output_index++;
	}

	free(bin);
	free(samples);
	return output;
}
```

### Updating Pixmap
This will update Pixmap for every GIF frame and Audio bars.
```C
void *update_pixmap(void *arg) {
	Arg_Struct *arg_struct = (Arg_Struct *)arg;

	uint8_t i = 0;
	uint8_t N = (arg_struct->gif)->frames;
	uint64_t start = get_milis();

    // Check if worker thread is still running
	while (atomic_load(&worker_thread_running) > 0) {

        // Check if pixmap needs update. Decided by main thread.
		if (atomic_load(&need_pixmap_update) > 0) {
            // Draw one frame of GIF image
			draw_image(arg_struct->pixmap, arg_struct->gif, i);

            // Checking interval between each frame in GIF image
			uint64_t end = get_milis();
			if (end - start > (arg_struct->gif)->delay) {
				i = (i % (N - 1)) + 1;
				start = get_milis();
			}
			uint64_t *band = get_bands(arg_struct->wav, arg_struct->mono_buffer,
			                           arg_struct->audio_frames_in_time,
			                           *(arg_struct->offset));

			if (band == NULL) {
                // Change state of atomic variable after update is failed.
				atomic_store(&need_pixmap_update, 0);
				continue;
			}

			for (uint32_t i = 0; i < total_bars; i++) {
                // Height of each bar depending upon strength of frequency
				float new_h =
				    (float)((uint64_t)band[i] * (uint64_t)HEIGHT / 2 / MAX_AMP);
				create_bar(arg_struct->pixmap, (arg_struct->bar)[i].position,
				           bar_width, new_h, 0xFFFFFF);
			}


            // Change state of atomic variable after update is done.
			atomic_store(&need_pixmap_update, 0);
		}
	}
	return NULL;
}
```
### Main function
This is the entry point of our program.
```C
int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Provide WAV and GIF file\n");
		return 0;
	}
```

Loading the WAV audio and GIF image into the memory
```C
    // Initialize WAV struct
	WAV wav = {0};
    // This will load struct with audio information
	if (load_wav(argv[1], &wav) == 0) return 0;

	uint16_t *mono_buffer = NULL;

    // Converting stereo audio PCM into mono
	if (get_mono_samples(&wav, &mono_buffer) == 0) return 0;

    // Initialize GIF struct
	GIF gif = {0};
    // This will load struct with GIF information
	if (load_gif(argv[2], &gif) == 0) return 0;
```

Creating XCB connection and initiazling Window, GContext and Pixmap
```C
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	xcb_window_t window = setup_window(conn, screen);

	xcb_map_window(conn, window);

	xcb_gcontext_t gc = setup_gcontext(conn, screen, window);

	xcb_pixmap_t pixmap = xcb_generate_id(conn);
	xcb_create_pixmap(conn, screen->root_depth, pixmap, window, WIDTH, HEIGHT);
```
1. `xcb_connect(NULL, NULL)` will return ID of xcb connection.
1. `xcb_setup_roots_iterator(xcb_get_setup(conn))` will provide all information about currect screen used by system.
1. `setup_window(conn, screen)` will create a GUI window with specific WIDTH and HEIGHT.
1. `xcb_map_window(conn, window)` will display the window on screen.
1. `setup_gcontext(conn, screen, window)` will create Graphical context for us to draw GUI elements, like pixmap.
1. `xcb_pixmap_t` is unique ID for pixmap which we are going to use.
1. `xcb_create_pixmap(...)` will create the pixmap. This pixmap will be used by GContext.

Creating Pixmap buffer

```C
    // Getting Bits per pixel for current screen. Usually 24 or 32 for color displays
	uint8_t bpp = bpp_by_depth(conn, screen->root_depth);
	uint32_t bytes_per_row = WIDTH * (bpp / 8);
	uint32_t total_bytes = bytes_per_row * HEIGHT;

	Pixmap pix = {.bpp = bpp,
	              .bytes_per_pixel = bpp / 8,
	              .bytes_per_row = bytes_per_row,
	              .total_bytes = bytes_per_row * HEIGHT,
	              .buffer = (uint8_t *)malloc(sizeof(uint8_t) * total_bytes)};
```

Creating intitial audio bars
```C
    // Checking if number of bars are in range of WIDTH of window
	if (total_bars * bar_width + gap * total_bars > WIDTH) {
		printf("Too Many bars for this Window");
		return 0;
	}

    // Creating all bars with initial length
	Bar bar[total_bars];
	for (uint16_t i = 0; i < total_bars; i++) {
		Bar b;
		b.length = 1;
		b.position = i * (bar_width + gap);
		b.rev = 0;
		bar[i] = b;
	}
```

Initializing ALSA Sound for audio playback.
```C
	int err;
	snd_pcm_t *handle;
	snd_pcm_sframes_t frames;

    // Opening default device for audio playback
	if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Playback open error: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}

    // Configuring device for type PCM we are going to use
	if ((err = snd_pcm_set_params(
	         handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
	         wav.nbr_channels, wav.frequency, 1, 5000)) < 0) { /* 0.5sec */
		printf("Playback open error: %s\n", snd_strerror(err));
		exit(EXIT_FAILURE);
	}
```

Calculating audio frames for ALSA sound and FFT.
```C
	uint32_t audio_frames_per_second = wav.bytes_per_second / wav.frame_size;
	uint32_t total_audio_frames = wav.sample_size / wav.frame_size;
	uint32_t audio_frames_in_time = (float)audio_frames_per_second / (float)1000 * (float)interval;
	uint32_t offset = 0;
```
1. If audio is stereo then it will have 2 channels left and right. Each channel will be of 2 bytes. Here a frame is group of 2 channels (left +  right).
2. A frame will be of 4 bytes.
3. `audio_frames_in_time` is amount of frames we will have in `interval`, which is 23 miliseconds. This is for syncing audio with visualization.
4. `offset` is to determine on what frame group we are currently.

Running worker thread
```C
    // Creating Argument for worker thread
	Arg_Struct arg_struct = {.pixmap = &pix,
	                         .mono_buffer = mono_buffer,
	                         .bar = &bar[0],
	                         .gif = &gif,
	                         .audio_frames_in_time = audio_frames_in_time,
	                         .offset = &offset};

	pthread_t worker_thread;
	pthread_create(&worker_thread, NULL, &update_pixmap, (void *)&arg_struct);
```

Starting GUI loop
```C
    // if 1, loop will end and program will exit
	int quit = 0;
	while (!quit) {
        // This will poll for any button or mouse event
		xcb_generic_event_t *event = xcb_poll_for_event(conn);

		if (event) {
            // 0x80 mask is to determine if event is explicit and not by server
			switch (event->response_type & ~0x80) {
				case XCB_EXPOSE:
                    // if Window is open, then show pixmap
					xcb_copy_area(conn, pixmap, window, gc, 0, 0, 0, 0, WIDTH,
					              HEIGHT);
					xcb_flush(conn);
					break;
				case XCB_KEY_PRESS:
                    // if ESC key is pressed, exit the loop
					xcb_key_press_event_t *env = (xcb_key_press_event_t *)event;
					if (env->detail == 9) quit = 1;
					break;
			}
		}
```

Handling audio and bar visualization
```C
        // Set flag to indicate worker thread that we need pixmap update
		atomic_store(&need_pixmap_update, 1);

        // Play audio using alsa sound
		frames = snd_pcm_writei(handle, wav.buffer + offset * wav.frame_size,
		                        audio_frames_in_time);
		if (frames < 0) frames = snd_pcm_recover(handle, frames, 0);

        // Move to next frame group
		offset = offset + audio_frames_in_time;

        // If out of frames, then exit loop
		if (offset > total_audio_frames) quit = 1;

        // If no flag is set by worker thread, it means it is still updating pixmap.
		if (atomic_load(&need_pixmap_update) == 1) continue;

        // Copies pixel data from buffer to pixmap
		xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, gc, WIDTH,
		              HEIGHT, 0, 0, 0, screen->root_depth, pix.total_bytes,
		              pix.buffer);
        // Copiex pixel data from pixmap to GContext
		xcb_copy_area(conn, pixmap, window, gc, 0, 0, 0, 0, WIDTH, HEIGHT);
        // Send request to X11 server
		xcb_flush(conn);
	}
```

Closing all connections and freeing memory
```C
	err = snd_pcm_drain(handle);
	if (err < 0) printf("snd_pcm_drain failed: %s\n", snd_strerror(err));
	snd_pcm_close(handle);

	atomic_store(&worker_thread_running, 0);
	pthread_join(worker_thread, NULL);

	free(pix.buffer);
	free(wav.buffer);
	free(gif.buffer);
	free(mono_buffer);
	xcb_free_pixmap(conn, pixmap);
	xcb_free_gc(conn, gc);
	xcb_disconnect(conn);
	return 0;
}
```

To run this project, you can clone it from [here](https://github.com/RudraSama/audio_visualizer_in_xcb).

Installing dependencies
- Arch - `sudo pacman -S alsa-lib libxcb xcb-util`
- Ubuntu - `sudo apt install libasound2-dev libxcb1-dev libxcb-util-dev`

Building project

`make`

Running project

`./audio_visualizer /path/to/wav/file.wav /path/to/gif/file.gif`

If compilation was successful, you will see this on output.

![](output_720p.mp4)

That’s pretty much the core logic. There’s more fine-tuning and optimization I might add later, but this covers the main part of how it works.

> This is not the perfect and most optimised code. You are free make it more better.
