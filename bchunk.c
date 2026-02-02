
 /*
  *	binchunker for Unix
  *	Copyright (C) 1998-2004  Heikki Hannikainen <hessu@hes.iki.fi>
  *
  *  This program is free software; you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *  the Free Software Foundation; either version 2 of the License, or
  *  (at your option) any later version.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *  GNU General Public License for more details.
  *
  *  You should have received a copy of the GNU General Public License
  *  along with this program; if not, write to the Free Software
  *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>

#define VERSION "1.3.0"
#define USAGE "Usage: bchunk [-v] [-r] [-p (PSX)] [-w (wav)] [-s (swabaudio)]\n" \
        "         <image.bin> <image.cue> <basename>\n" \
	"Example: bchunk foo.bin foo.cue foo\n" \
	"  -v  Verbose mode\n" \
	"  -r  Raw mode for MODE2/2352: write all 2352 bytes from offset 0 (VCD/MPEG)\n" \
	"  -p  PSX mode for MODE2/2352: write 2336 bytes from offset 24\n" \
	"      (default MODE2/2352 mode writes 2048 bytes from offset 24)\n"\
	"  -w  Output audio files in WAV format\n" \
	"  -m  Merge and convert bin files based on files in .cue (experimental)\n" \
	"      in this mode <image.bin> is the path to a new file that will be created\n" \
	"      a matching <image.cue> file will be produced to accompany the merged bin\n" \
	"      dropping the <basename> argument in merge mode performs the bin/cue\n" \
	"      merge without creating additional output files (eg. WAV, ISO, etc.)\n" \
	"  -s  swabaudio: swap byte order in audio tracks\n"
	
#define VERSTR	"binchunker for Unix, version " VERSION " by Heikki Hannikainen <hessu@hes.iki.fi>\n" \
		"\tCreated with the kind help of Bob Marietta <marietrg@SLU.EDU>,\n" \
		"\tpartly based on his Pascal (Delphi) implementation.\n" \
		"\tSupport for MODE2/2352 ISO tracks thanks to input from\n" \
		"\tGodmar Back <gback@cs.utah.edu>, Colas Nahaboo <Colas@Nahaboo.com>\n" \
		"\tand Matthew Green <mrg@eterna.com.au>.\n" \
		"\tReleased under the GNU GPL, version 2 or later (at your option).\n\n"

#define CUELLEN 1024
#define SECTLEN 2352

#define WAV_RIFF_HLEN 12
#define WAV_FORMAT_HLEN 24
#define WAV_DATA_HLEN 8
#define WAV_HEADER_LEN WAV_RIFF_HLEN + WAV_FORMAT_HLEN + WAV_DATA_HLEN

/*
 *	Ugly way to convert integers to little-endian format.
 *	First let netinet's hton() functions decide if swapping should
 *	be done, then convert back.
 */

#include <inttypes.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

#define bswap_16(x) \
     ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |  \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

#define htoles(x) bswap_16(htons(x))
#define htolel(x) bswap_32(htonl(x))



struct track_t {
	int num;
	int mode;
	int audio;
	char *modes;
	char *extension;
	int bstart;
	int bsize;
	int32_t startsect;
	int32_t stopsect;
	int32_t start;
	int32_t stop;
	struct track_t *next;
};

char *basefile = NULL;
char *binfile = NULL;
char *cuefile = NULL;
char *cuefile_copy = NULL;
char *bindir = NULL;
int verbose = 0;
int psxtruncate = 0;
int raw = 0;
int swabaudio = 0;
int towav = 0;
int merge = 0;
int nowrite = 0;

/*
 *	Parse arguments
 */

void parse_args(int argc, char *argv[])
{
	int s;
	
	while ((s = getopt(argc, argv, "swvp?hrm")) != -1) {
		switch (s) {
			case 'r':
				raw = 1;
				break;
			case 'm':
				merge = 1;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'w':
				towav = 1;
				break;
			case 'p':
				psxtruncate = 1;
				break;
			case 's':
				swabaudio = 1;
				break;
			case '?':
			case 'h':
				fprintf(stderr, "%s", USAGE);
				exit(0);
		}
	}

	if (argc - optind == 3) {
		while (optind < argc) {
			switch (argc - optind) {
				case 3:
					binfile = strdup(argv[optind]);
					break;
				case 2:
					cuefile = strdup(argv[optind]);
					cuefile_copy = strdup(cuefile);
					bindir = dirname(cuefile_copy);
					break;
				case 1:
					basefile = strdup(argv[optind]);
					break;
				default:
					fprintf(stderr, "%s", USAGE);
					exit(1);
			}
			optind++;
		}
	} else if ((argc - optind == 2) && merge) {
		nowrite = 1;
		while (optind < argc) {
			switch (argc - optind) {
				case 2:
					binfile = strdup(argv[optind]);
					break;
				case 1:
					cuefile = strdup(argv[optind]);
					cuefile_copy = strdup(cuefile);
					bindir = dirname(cuefile_copy);
					break;
				default:
					fprintf(stderr, "%s", USAGE);
					exit(1);
			}
			optind++;
		}
	} else {
		fprintf(stderr, "%s", USAGE);
		exit(1);
	}
}

/*
 *	Convert a mins:secs:frames format to plain frames
 */

int32_t time2frames(char *msf)
{
	int mins = 0, secs = 0, frames = 0;
	char *p, *t, *s;
	s = strdup(msf);
	
	if (!(p = strchr(s, ':')))
		return -1;
	*p = '\0';
	mins = atoi(s);
	
	p++;
	if (!(t = strchr(p, ':')))
		return -1;
	*t = '\0';
	secs = atoi(p);
	
	t++;
	frames = atoi(t);
	
	return 75 * (mins * 60 + secs) + frames;
}

void frames2time(int32_t totalframes, char* msfstring)
{
	int mins = totalframes / (75*60);
	int secs = (totalframes - mins * (75*60)) / 75;
	int frames = totalframes - mins * (75*60) - secs * (75);

	snprintf(msfstring,50,"%02d" ":" "%02d" ":" "%02d",mins,secs,frames);

}

/*
 *	Parse the mode string
 */

void gettrackmode(struct track_t *track, char *modes)
{
	static char ext_iso[] = "iso";
	static char ext_cdr[] = "cdr";
	static char ext_wav[] = "wav";
	static char ext_ugh[] = "ugh";
	
	track->audio = 0;
	
	if (!strcasecmp(modes, "MODE1/2352")) {
		track->bstart = 16;
		track->bsize = 2048;
		track->extension = ext_iso;
		
	} else if (!strcasecmp(modes, "MODE2/2352")) {
		track->extension = ext_iso;
		if (raw) {
			/* Raw MODE2/2352 */
			track->bstart = 0;
			track->bsize = 2352;
		} else if (psxtruncate) {
			/* PSX: truncate from 2352 to 2336 byte tracks */
			track->bstart = 0;
			track->bsize = 2336;
		} else {
			/* Normal MODE2/2352 */
			track->bstart = 24;
			track->bsize = 2048;
		}
		
	} else if (!strcasecmp(modes, "MODE2/2336")) {
		/* WAS 2352 in V1.361B still work?
		 * what if MODE2/2336 single track bin, still 2352 sectors?
		 */
		track->bstart = 16;
		track->bsize = 2336;
		track->extension = ext_iso;
		
	} else if (!strcasecmp(modes, "AUDIO")) {
		track->bstart = 0;
		track->bsize = 2352;
		track->audio = 1;
		if (towav)
			track->extension = ext_wav;
		else
			track->extension = ext_cdr;
	} else {
		printf("(?) ");
		track->bstart = 0;
		track->bsize = 2352;
		track->extension = ext_ugh;
	}
}

/*
 *	return a progress bar
 */

char *progressbar(float f, int l)
{
	static char s[80];
	int i, n;
	
	n = l * (int)f;
	for (i = 0; i < n; i++) {
		s[i] = '*';
	}
	for (; i < l; i++) {
		s[i] = ' ';
	}
	s[i] = '\0';
	
	return s;
}

/*
 *	Write a track
 */

int writetrack(FILE *bf, struct track_t *track, char *bname)
{
	char *fname;
	FILE *f;
	char buf[SECTLEN+10];
	long sz, sect, realsz, reallen;
	char c, *p, *p2, *ep;
	int32_t l;
	int16_t i;
	float fl;
	
	if (asprintf(&fname, "%s%2.2d.%s", bname, track->num, track->extension) == -1) {
		fprintf(stderr, "writetrack(): asprintf() failed, out of memory\n");
		exit(4);
	}
	
	printf("%2d: %s ", track->num, fname);
	
	if (!(f = fopen(fname, "wb"))) {
		fprintf(stderr, " Could not fopen track file: %s\n", strerror(errno));
		exit(4);
	}
	
	if (fseek(bf, track->start, SEEK_SET)) {
		fprintf(stderr, " Could not fseek to track location: %s\n", strerror(errno));
		exit(4);
	}
	
	reallen = (track->stopsect - track->startsect + 1) * track->bsize;
	if (verbose) {
		printf("\n mmc sectors %" PRId32"->%" PRId32" (%d)", track->startsect, track->stopsect, track->stopsect - track->startsect + 1);
		printf("\n mmc bytes %" PRId32 "->%" PRId32" (%d)", track->start, track->stop, track->stop - track->start + 1);
		printf("\n sector data at %d, %d bytes per sector", track->bstart, track->bsize);
		printf("\n real data %d bytes", (track->stopsect - track->startsect + 1) * track->bsize);
		printf("\n");
	}

	printf("                                          ");
	
	if ((track->audio) && (towav)) {
		// RIFF header
		fputs("RIFF", f);
		l = htolel(reallen + WAV_DATA_HLEN + WAV_FORMAT_HLEN + 4);
		fwrite(&l, 4, 1, f);  // length of file, starting from WAVE
		fputs("WAVE", f);
		// FORMAT header
		fputs("fmt ", f);
		l = htolel(0x10);     // length of FORMAT header
		fwrite(&l, 4, 1, f);
		i = htoles(0x01);     // constant
		fwrite(&i, 2, 1, f);
		i = htoles(0x02);	// channels
		fwrite(&i, 2, 1, f);
		l = htolel(44100);	// sample rate
		fwrite(&l, 4, 1, f);
		l = htolel(44100 * 4);	// bytes per second
		fwrite(&l, 4, 1, f);
		i = htoles(4);		// bytes per sample
		fwrite(&i, 2, 1, f);
		i = htoles(2*8);	// bits per channel
		fwrite(&i, 2, 1, f);
		// DATA header
		fputs("data", f);
		l = htolel(reallen);
		fwrite(&l, 4, 1, f);
	}
	
	realsz = 0;
	sz = track->start;
	sect = track->startsect;
	fl = 0;
	while ((sect <= track->stopsect) && (fread(buf, SECTLEN, 1, bf) > 0)) {
		if (track->audio) {
			if (swabaudio) {
				/* swap low and high bytes */
				p = &buf[track->bstart];
				ep = p + track->bsize;
				while (p < ep) {
					p2 = p + 1;
					c = *p;
					*p = *p2;
					*p2 = c;
					p += 2;
				}
			}
		}
		if (fwrite(&buf[track->bstart], track->bsize, 1, f) < 1) {
			fprintf(stderr, " Could not write to track: %s\n", strerror(errno));
			exit(4);
		}
		sect++;
		sz += SECTLEN;
		realsz += track->bsize;
		if (((sz / SECTLEN) % 500) == 0) {
			fl = (float)realsz / (float)reallen;
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b%4ld/%-4ld MB  [%s] %3.0f %%", realsz/1024/1024, reallen/1024/1024, progressbar(fl, 20), fl * 100);
			fflush(stdout);
		}
	}
	
	fl = (float)realsz / (float)reallen;
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b%4ld/%-4ld MB  [%s] %3.0f %%", realsz/1024/1024, reallen/1024/1024, progressbar(1, 20), fl * 100);
	fflush(stdout);
	
	if (ferror(bf)) {
		fprintf(stderr, " Could not read from %s: %s\n", binfile, strerror(errno));
		exit(4);
	}
	
	if (fclose(f)) {
		fprintf(stderr, " Could not fclose track file: %s\n", strerror(errno));
		exit(4);
	}
	
	printf("\n");
	return 0;
}

/*
 *	Main
 */

int main(int argc, char **argv)
{
	char s[CUELLEN+1];
	char *p, *t;
	char shiftedmsf[50];
	char *mergedcue, *ext_pos;
	struct track_t *tracks = NULL;
	struct track_t *track = NULL;
	struct track_t *prevtrack = NULL;
	struct track_t **prevp = &tracks;
	int32_t nextsectoroffset = 0;
	int32_t prevsectoroffset = 0;
	
	FILE *binf = NULL, *cuef = NULL, *mergef = NULL, *mergecf = NULL;

	printf("%s", VERSTR);
	
	parse_args(argc, argv);

	
	if (!merge) {
		if (!((binf = fopen(binfile, "r")))) {
			fprintf(stderr, "Could not open BIN %s: %s\n", binfile, strerror(errno));
			return 2;
		}
	} else {
		// create path to matching cue file for merged bin
		mergedcue = strdup(binfile);
		ext_pos = strstr(mergedcue, ".bin");
		if (!(ext_pos == NULL) && mergedcue[0] != '\0') {
			strcpy(ext_pos,".cue");
		}else {
			fprintf(stderr, "Bin file name must end in .bin for merge mode\n");
			return 2;
		}
		// open merged bin and cue files
		if (!((mergef = fopen(binfile, "w+bx")))) {
			fprintf(stderr, "Could not open merged BIN %s: %s\n",binfile,strerror(errno));
			return 2;
		}
		if (!((mergecf = fopen(mergedcue, "w+x")))) {
			fprintf(stderr, "Could not open merged CUE %s: %s\n",mergedcue,strerror(errno));
			return 2;
		}
		fprintf(mergecf, "FILE \"%s\" BINARY\n", binfile);
	}
	
	if (!((cuef = fopen(cuefile, "r")))) {
		fprintf(stderr, "Could not open CUE %s: %s\n", cuefile, strerror(errno));
		return 2;
	}

	if (verbose)
		printf("Path to BIN/CUE files:%s\n",bindir);
	
	/* This unnecessarily ate the first line of the cue file
	 * which is usually used for file definitions. removed this
	 * to handle file descriptions correctly in -m (merge) mode
	 */
	/* We don't really care about the first line. */
	/*if (!fgets(s, CUELLEN, cuef)) {
		fprintf(stderr, "Could not read first line from %s: %s\n", cuefile, strerror(errno));
		return 3;
	}
	*/

	while (fgets(s, CUELLEN, cuef)) {
		while ((p = strchr(s, '\r')) || (p = strchr(s, '\n')))
			*p = '\0';
			
		if (!strstr(s, "FILE") && !strstr(s, "INDEX") && merge)
			fprintf(mergecf, "%s\n",s); // copy track line as-is

		if ((p = strstr(s, "TRACK"))) {
			printf("\nTrack ");
			if (!(p = strchr(p, ' '))) {
				fprintf(stderr, "... ouch, no space after TRACK.\n");
				exit(3);
			}
			p++;
			if (!(t = strchr(p, ' '))) {
				fprintf(stderr, "... ouch, no space after track number.\n");
				exit(3);
			}
			*t = '\0';
			
			prevtrack = track;
			if (!(track = malloc(sizeof(struct track_t)))) {
				fprintf(stderr, "main(): malloc() failed, out of memory\n");
				exit(4);
			}
			*prevp = track;
			prevp = &track->next;
			track->next = NULL;
			track->num = atoi(p);
			
			p = t + 1;
			printf("%2d: %-12.12s ", track->num, p);
			track->modes = strdup(p);
			track->extension = NULL;
			track->mode = 0;
			track->audio = 0;
			track->bsize = track->bstart = -1;
			track->bsize = -1;
			track->startsect = track->stopsect = -1;
			
			gettrackmode(track, p);
			
		} else if ((p = strstr(s, "INDEX"))) {
			if (!(p = strchr(p, ' '))) {
				printf("... ouch, no space after INDEX.\n");
				exit(3);
			}
			p++;
			if (!(t = strchr(p, ' '))) {
				printf("... ouch, no space after index number.\n");
				exit(3);
			}
			*t = '\0';
			t++;
			printf(" %s %s", p, t);

			if(merge) {
				frames2time(prevsectoroffset+time2frames(t), shiftedmsf);
				fprintf(mergecf, "    INDEX %s %s\n", p, shiftedmsf);
			}
			if (strcmp(p,"01") == 0) {
				track->startsect = prevsectoroffset + time2frames(t);
				track->start = track->startsect * SECTLEN;
				if (verbose)
					printf(" (startsect %" PRId32 " ofs %" PRId32 ")", track->startsect, track->start);
				if ((prevtrack) && (prevtrack->stopsect < 0)) {
					prevtrack->stopsect = track->startsect - 1;
					prevtrack->stop = track->start - 1;
				}
			} else if ((strcmp(p,"00") == 0) && (track->num == 1) && (track->audio == 1)) {
				printf("detected pregap track at track one\n");
				track->num = 0;
				track->startsect = prevsectoroffset + time2frames(t);
				track->start = track->startsect * SECTLEN;
				

				prevtrack = track;
				if (!(track = malloc(sizeof(struct track_t)))) {
					fprintf(stderr, "main(): malloc() failed, out of memory\n");
					exit(4);
				}

				*prevp = track;
				prevp = &track->next;
				track->next = NULL;
				track->num = 1;

				track->modes = strdup(prevtrack->modes);
				track->extension = prevtrack->extension;
				track->mode = prevtrack->mode;
				track->audio = prevtrack->audio;
				track->bsize = prevtrack->bsize;
				track->bstart = prevtrack->bstart;
				track->startsect = track->stopsect = -1;

				// add commands to create an additional track here
				// same configuration as this track

			}
		} else if ((p = strstr(s, "FILE"))) {
			// detected a bin file descriptor
			if (!(p = strchr(p, '"'))) {
				printf("... did not detect open quote for bin file.\n");
				exit(3);
			}
			p++;
			
			if (!(t = strchr(p, '"'))) {
				printf("... did not detect closing quote for bin file.\n");
				exit(3);
			}
			*t = '\0';
			t++;
			
			if (merge) {
				char fullpath[200];
				snprintf(fullpath, sizeof(fullpath), "%s/%s", bindir, p);
				printf("\nLoading new BIN file: %s", fullpath);
				if (!((binf = fopen(fullpath, "r")))) {
					fprintf(stderr, "Could not open BIN %s: %s\n", p, strerror(errno));
					return 2;
				}
			
				prevsectoroffset = nextsectoroffset;

				unsigned char buffer[8192];
				size_t bytesRead;
			
				// copy bytes from new file into merged file
				while ((bytesRead = fread(buffer, 1, sizeof(buffer), binf)) > 0) {
        				if (fwrite(buffer, 1, bytesRead, mergef) != bytesRead) {
            					fprintf(stderr,"Error writing to merged BIN file\n");
						return 2;
            				break;
        				}
				}

				fseek(binf, 0, SEEK_END);
				nextsectoroffset = prevsectoroffset + (1+((int32_t)ftell(binf) - 1) / SECTLEN);

				fclose(binf);
			}
		}
	}
	
	if (merge) {
		if (track) {
			fseek(mergef, 0, SEEK_END);
			track->stop = (int32_t)ftell(mergef) - 1;
			track->stopsect = track->stop / SECTLEN;
		}
	} else {
		if (track) {
			fseek(binf, 0, SEEK_END);
			track->stop = (int32_t)ftell(binf) - 1;
			track->stopsect = track->stop / SECTLEN;
		}
	}

	printf("\n\n");
	
	if (!nowrite) {
		printf("Writing tracks:\n\n");
		for (track = tracks; (track); track = track->next) {
			if (merge) {
				writetrack(mergef, track, basefile);
			} else {
				writetrack(binf, track, basefile);
			}
		}
	
	}

	if (merge) {
		fclose(mergef);
	} else {
		fclose(binf);
	}
	fclose(cuef);

	return 0;
}

