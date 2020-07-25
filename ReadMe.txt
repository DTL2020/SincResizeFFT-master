It is simple 1D FFT equivalent to Sinc-resize using FFT lib.
It input in.tif file (RGB 8bit uncompressed) and 1 command line param (x-scale integer, 4 default).
Output is 1D-width upsized out.tif. To get 2D V+H resized you need to 90-degree rotate in external software and feed to input as in.tif again.
May be will be added internal rotation later for 1-pass processing.

Build release and samples are in the Release-master.zip file.

Build using Visual Studio 6 (win32 only for now). Also included .lib and .dlls from LibTIFF and FFTW3 library (if illegal - you need to download them separately and will be removed from repository).
