// Sinc2D.cpp : Defines the entry point for the console application.
//


#include "stdafx.h"
#include "windows.h"
#include "commctrl.h"

#include "fftw3.h"
#include "tiffio.h"

#include "math.h"
#include "stdlib.h"


BYTE *g_pImageBuffer = 0, *g_pFilteredImageBuffer = 0;

float *g_pfImageBuffer = 0, *g_pfFilteredImageBuffer = 0;

int iMul = 4;
float fSigma=0.05f;

int iWidth=2000;
int iHeight=2000;

// FFT stuff
float *pfLine_sign;
fftwf_complex * pfLine_FFTed;
fftwf_plan Plan_FFTfw;

fftwf_complex * pfLine_4IFFT_int;
fftwf_plan Plan_FFTbw_int;
float * pfLine_IFFTed_int;

//-----------------------------------------------------------------------------
// Msg: Display an error if needed
//-----------------------------------------------------------------------------
void Msg(TCHAR *szFormat, ...)
{
    printf(szFormat);
}

BOOL LoadTIFF8(void)
{
	TIFF *in_tiff;
	unsigned short bps, spp, cmpr;
	tstrip_t strip;
	uint32* bc;
	uint32 stripsize, rowspstrip;
	uint32 uiWidth, uiHeight;
	uint32 row, col;

	unsigned char *ucSrc_data;
	unsigned char *ucImageBuffer = (unsigned char*)g_pImageBuffer;

	// Open the src TIFF image
	if((in_tiff = TIFFOpen("in.tif", "r")) == NULL)
	{
		Msg(TEXT("Could not open src TIFF image"));
		return FALSE;
	}
	
	// Check that it is of a type that we support
	if((TIFFGetField(in_tiff, TIFFTAG_COMPRESSION, &cmpr) ==0 ) || (cmpr != COMPRESSION_NONE))
	{
		Msg(TEXT("Compressed TIFFs not supported"));
		TIFFClose(in_tiff);
		return FALSE;
	}

	if((TIFFGetField(in_tiff, TIFFTAG_BITSPERSAMPLE, &bps) == 0) || (bps != 8))
	{
		Msg(TEXT("Either undefined or unsupported number of bits per sample - must be 8"));
		TIFFClose(in_tiff);
		return FALSE;
	}

	if((TIFFGetField(in_tiff, TIFFTAG_SAMPLESPERPIXEL, &spp) == 0) || (spp != 3))
	{
		Msg(TEXT("Either undefined or unsupported number of samples per pixel - must be 3"));
		TIFFClose(in_tiff);
		return FALSE;
	}

	TIFFGetField(in_tiff, TIFFTAG_IMAGEWIDTH, &uiWidth);
	iWidth = uiWidth;

	TIFFGetField(in_tiff, TIFFTAG_IMAGELENGTH, &uiHeight);
	iHeight = uiHeight;

	ucSrc_data = (unsigned char*)malloc(uiWidth*uiHeight*3);

	TIFFGetField(in_tiff, TIFFTAG_STRIPBYTECOUNTS, &bc);
	stripsize = bc[0];
	// test if all strips are equal
	for (strip = 0; strip < TIFFNumberOfStrips(in_tiff); strip++) 
	{
		if (bc[strip] != stripsize) 
		{
			Msg(TEXT("Strip sizes unequal"));
//			return FALSE;
		}
	}

	TIFFGetField(in_tiff, TIFFTAG_ROWSPERSTRIP, &rowspstrip);

	// load from tiff to temp buffer
	for(row = 0; row < uiHeight; row+=rowspstrip)
	{
		TIFFReadRawStrip(in_tiff, row/rowspstrip, &ucSrc_data[row*uiWidth*3], stripsize);
	}

	// separate planes
	for(row=0; row < uiHeight; row++)
	{
		for(col=0; col < uiWidth; col++)
		{
			g_pImageBuffer[(row*uiWidth + col)] = ucSrc_data[(row*uiWidth+col)*3+0]; // R
			g_pImageBuffer[(uiWidth*uiHeight + row*uiWidth + col)] = ucSrc_data[(row*uiWidth+col)*3+1]; // G
			g_pImageBuffer[(uiWidth*uiHeight*2+ row*uiWidth + col)] = ucSrc_data[(row*uiWidth+col)*3+2]; // B
		}
	}

	free(ucSrc_data);
	TIFFClose(in_tiff);

	return TRUE;
}

int SaveTIFF8()
{
	TIFF *out_tiff;
	// Open the dst TIFF image
	if((out_tiff = TIFFOpen("out.tif", "w")) == NULL)
	{
		fprintf(stderr, "Could not open dst TIFF image\n");
		return(1);
	}


	TIFFSetField(out_tiff, TIFFTAG_IMAGEWIDTH, iWidth*iMul);
	TIFFSetField(out_tiff, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(out_tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
	TIFFSetField(out_tiff, TIFFTAG_ROWSPERSTRIP, 1);
	TIFFSetField(out_tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(out_tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	TIFFSetField(out_tiff, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
	TIFFSetField(out_tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

	TIFFSetField(out_tiff, TIFFTAG_XRESOLUTION, 300);
	TIFFSetField(out_tiff, TIFFTAG_YRESOLUTION, 300);
	TIFFSetField(out_tiff, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);

	TIFFSetField(out_tiff, TIFFTAG_IMAGELENGTH, iHeight);

	unsigned char *ucDst_data  = (unsigned char*)malloc(iWidth*3*iMul);

	for (int row=0; row < iHeight; row++) // lines counter
		{

			for (int i = 0; i < iWidth*iMul; i++)
			{
				ucDst_data[i*3] = g_pFilteredImageBuffer[(row*iWidth*iMul + i)]; // R channel
				ucDst_data[i*3+1] = g_pFilteredImageBuffer[(iWidth*iHeight*iMul + row*iWidth*iMul + i)]; // G channel
				ucDst_data[i*3+2] = g_pFilteredImageBuffer[(iWidth*iHeight*2*iMul + row*iWidth*iMul + i)]; // B channel
			}
		
			TIFFWriteRawStrip(out_tiff, row, ucDst_data, /*g_stripsize*/iWidth*3*iMul);
	}

	TIFFClose(out_tiff);

	free (ucDst_data);

	return 0;

}

void Proc(void)
{
	int row,col,i;
	
	for (row=0; row < iHeight; row++) // lines counter
	{
		// R-channel
		for (col = 0; col < iWidth; col++)
		{
			pfLine_sign[col] = (float)g_pImageBuffer[(row*iWidth + col)]; // R channel
		}

		fftwf_execute(Plan_FFTfw);

		// normalizing and copy to bw buffer
		for (i = 0; i < iWidth/2; i++) 
		{
			pfLine_4IFFT_int[i][0] = pfLine_FFTed[i][0] / iWidth;
			pfLine_4IFFT_int[i][1] = pfLine_FFTed[i][1] / iWidth;
		}
		// adding zeroes
		for (i = (iWidth/2); i < iWidth*iMul/2+1; i++)
		{
			pfLine_4IFFT_int[i][0] = 0.0f;
			pfLine_4IFFT_int[i][1] = 0.0f;
		}

		fftwf_execute(Plan_FFTbw_int);

		for (col = 0; col < iWidth*iMul; col++)
		{
			unsigned char ucVal;
			float fVal = pfLine_IFFTed_int[col]; // R channel
			if (fVal > 255.0f) 
			{
				fVal= 255.0f;
			}
			if (fVal < 0.0f) 
			{
				fVal = 0.0f;
			}
			ucVal = (unsigned char) (fVal+0.5f);
			g_pFilteredImageBuffer[(row*iWidth*iMul + col)] = ucVal; // R channel
//			g_pFilteredImageBuffer[(iWidth*iHeight*iMul + row*iWidth*iMul + col)] = ucVal; //temp to G 
//			g_pFilteredImageBuffer[(iWidth*iHeight*2*iMul + row*iWidth*iMul + col)] = ucVal; //temp to B 
		}		

	    // G-channel
	
		for (col = 0; col < iWidth; col++)
		{
			pfLine_sign[col] = (float)g_pImageBuffer[(iWidth*iHeight + row*iWidth + col)]; // G channel
		}

		fftwf_execute(Plan_FFTfw);

		// normalizing and copy to bw buffer
		for (i = 0; i < iWidth/2; i++) 
		{
			pfLine_4IFFT_int[i][0] = pfLine_FFTed[i][0] / iWidth;
			pfLine_4IFFT_int[i][1] = pfLine_FFTed[i][1] / iWidth;
		}
		// adding zeroes
		for (i = iWidth/2; i < iWidth*iMul/2; i++)
		{
			pfLine_4IFFT_int[i][0] = 0.0f;
			pfLine_4IFFT_int[i][1] = 0.0f;
		}

		fftwf_execute(Plan_FFTbw_int);

		for (col = 0; col < iWidth*iMul; col++)
		{
			unsigned char ucVal;
			float fVal = pfLine_IFFTed_int[col]; // G channel
			if (fVal > 255.0f) 
			{
				fVal= 255.0f;
			}
			if (fVal < 0.0f) 
			{
				fVal = 0.0f;
			}
			ucVal = (unsigned char) (fVal+0.5f);
			g_pFilteredImageBuffer[(iWidth*iHeight*iMul + row*iWidth*iMul + col)] = ucVal; // G-channel
		}		

	    // B-channel

		for (col = 0; col < iWidth; col++)
		{
			pfLine_sign[col] = (float)g_pImageBuffer[(iWidth*iHeight*2 + row*iWidth + col)]; // B channel
		}

		fftwf_execute(Plan_FFTfw);

		// normalizing and copy to bw buffer
		for (i = 0; i < iWidth/2; i++) 
		{
			pfLine_4IFFT_int[i][0] = pfLine_FFTed[i][0] / iWidth;
			pfLine_4IFFT_int[i][1] = pfLine_FFTed[i][1] / iWidth;
		}
		// adding zeroes
		for (i = iWidth/2; i < iWidth*iMul/2; i++)
		{
			pfLine_4IFFT_int[i][0] = 0.0f;
			pfLine_4IFFT_int[i][1] = 0.0f;
		}

		fftwf_execute(Plan_FFTbw_int);

		for (col = 0; col < iWidth*iMul; col++)
		{
			unsigned char ucVal;
			float fVal = pfLine_IFFTed_int[col]; // G channel
			if (fVal > 255.0f) 
			{
				fVal= 255.0f;
			}
			if (fVal < 0.0f) 
			{
				fVal = 0.0f;
			}
			ucVal = (unsigned char) (fVal+0.5f);
			g_pFilteredImageBuffer[(iWidth*iHeight*2*iMul + row*iWidth*iMul + col)] = ucVal; // B-channel
		}
		
		printf("Just %d row of %d done. Be patient...\r",row,iHeight+1);

	}

}

int main(int argc, char* argv[])
{
	if (argc > 1)
	{
		argv++;
		iMul=atoi(argv[0]);
	}
	
	printf("FFT-based Sinc-type upsizer, hor upsize only, resize to %dx\n",iMul);

	g_pImageBuffer = (BYTE*)malloc (iWidth * iHeight * 3);
	g_pFilteredImageBuffer = (BYTE*)malloc (iWidth *iMul * iHeight * 3);

	ZeroMemory(g_pFilteredImageBuffer,(iWidth *iMul * iHeight * 3));

	g_pfImageBuffer = (float*)malloc (iWidth * iHeight * 3 * sizeof(float));
	g_pfFilteredImageBuffer = (float*)malloc (iWidth *iMul * iHeight * 3 * sizeof(float));


	if (LoadTIFF8() == FALSE)
		goto end;

	  /////////////////
	 //  FFT inits ...
	/////////////////
	pfLine_sign = (float *) malloc(sizeof(float) * (iWidth+1));
	pfLine_FFTed = (float (*)[2])fftwf_malloc(sizeof(fftwf_complex) * iWidth);
	Plan_FFTfw = fftwf_plan_dft_r2c_1d(iWidth, pfLine_sign, pfLine_FFTed, FFTW_MEASURE);

	pfLine_IFFTed_int = (float *) malloc(sizeof(float) * (iWidth * iMul+1));
	pfLine_4IFFT_int = (float (*)[2])fftwf_malloc(sizeof(fftwf_complex) * (iWidth * iMul+1));
	Plan_FFTbw_int = fftwf_plan_dft_c2r_1d(iWidth*iMul, pfLine_4IFFT_int, pfLine_IFFTed_int, FFTW_MEASURE);

	Proc();

	SaveTIFF8();
end:
	free (g_pImageBuffer);
    free (g_pFilteredImageBuffer);

	free (g_pfImageBuffer);
	free (g_pfFilteredImageBuffer);

	free(pfLine_sign);
	free(pfLine_IFFTed_int);

	fftwf_free (pfLine_FFTed);
	fftwf_free (pfLine_4IFFT_int);

	fftwf_destroy_plan (Plan_FFTfw);
	fftwf_destroy_plan (Plan_FFTbw_int);

	return 0;
}

