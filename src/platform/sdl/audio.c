#include "ice.h"

#include <stdio.h>
#include <SDL2/SDL.h>

#include "lib/stb/stb_vorbis.c"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#define BUFFER_SIZE 2048

#define MAX_SAMPLES 32
#define MAX_SOURCES 64

typedef struct {
	unsigned char *data;
	unsigned int   rate;
	unsigned int   length;
} audio_sample;

typedef struct {
	unsigned int  sample_id;
	unsigned int  position;
	unsigned char state;
	unsigned char volume;
} audio_source;

audio_sample *samples = NULL;
audio_source *sources = NULL;

static SDL_AudioSpec audio_spec;

void sdl_audio_buffer(
	void *userdata,
	Uint8 *stream,
	int len
) {
	unsigned int mix_a;
	unsigned int mix_b;
	unsigned int mix_c;

	audio_sample *sample;
	audio_source *source;

	for (unsigned int i=0; i<len; i++) {
		stream[i]=127;

		for (unsigned int j=0; j<MAX_SOURCES; j++) {
			source = &sources[j];
			sample = &samples[source->sample_id];

			if (
				sample->data!=NULL &&
				source->state>ICE_AUDIO_STATE_PAUSED
			) {
				mix_a = (unsigned int)stream[i];
				mix_b = (unsigned int)sample->data[source->position];
				mix_c = (mix_a+mix_b)/2;

				stream[i]=(Uint8)mix_c;

				source->position += 1;
				source->position %= sample->length;

				if (
					source->position==0 &&
					source->state==ICE_AUDIO_STATE_PLAYING
				) {
					source->state=ICE_AUDIO_STATE_PAUSED;
				}
			}
		}
	};
}

ice_uint ice_audio_init() {
	audio_spec.freq     = 22050;
	audio_spec.format   = AUDIO_U8;
	audio_spec.channels = 1;
	audio_spec.samples  = BUFFER_SIZE;
	audio_spec.callback = sdl_audio_buffer;
	audio_spec.userdata = NULL;

	if (SDL_OpenAudio(
		&audio_spec,
		NULL
	)!=0) {
		char error_msg[256];

		sprintf(
			error_msg,
			"Failed to initialize audio: %s",
			SDL_GetError()
		);

		ice_log((ice_char *)error_msg);

		return 1;
	}

	samples=(audio_sample *)calloc(
		MAX_SAMPLES,
		sizeof(audio_sample)
	);
	sources=(audio_source *)calloc(
		MAX_SOURCES,
		sizeof(audio_source)
	);

	SDL_PauseAudio(0);

    return 0;
}

void ice_audio_deinit() {
	SDL_CloseAudio();

	ice_audio_sample_flush();
	ice_audio_source_flush();

	if (samples!=NULL) {
		free(samples);
		samples=NULL;
	}

	if (sources!=NULL) {
		free(sources);
		sources=NULL;
	}
}

void ice_audio_buffer(
	ice_real tick
) {
}

void ice_audio_sample_flush() {
	if (samples!=NULL) {
		for (ice_uint i=0; i<MAX_SAMPLES; i++) {
			ice_audio_sample_delete(i);
		}
	}
}

void ice_audio_source_flush() {
	if (sources!=NULL) {
		for (ice_uint i=0; i<MAX_SOURCES; i++) {
			ice_audio_source_delete(i);
		}
	}
}

ice_uint ice_audio_sample_load(
	ice_uint file_id
) {
	if (samples==NULL) {
		ice_log((ice_char *)"Audio is not initialized!");
		
		return 0;
	}
	
	ice_uint      sample_id = 0;
	audio_sample *sample    = NULL;
	
	for (ice_uint i=0; i<MAX_SAMPLES; i++) {
		if (samples[i].data==NULL) {
			sample_id = i;
			sample    = &samples[i];
			
			break;
		}
	}
	
	if (sample==NULL) {
		ice_log((ice_char *)"Exceeded audio samples limit");
		
		return 0;
	}
	
	char filename[32];
	sprintf(
		filename,
		"%u.ogg",
		(unsigned int)file_id
	);
	
	stb_vorbis *stream = stb_vorbis_open_filename(
		filename,
		NULL,
		NULL
	);
	
	if (stream==NULL) {
		ice_char msg[64];
		sprintf(
			(char *)msg,
			"Failed to load audio sample: %u",
			file_id
		);
		ice_log(msg);
		
		return 0;
	}
	
	stb_vorbis_info info = stb_vorbis_get_info(stream);
	
	sample->rate   = info.sample_rate;
	sample->length = stb_vorbis_stream_length_in_samples(stream);
	sample->data   = malloc(sample->length);
	
	if (sample->data==NULL) {
		ice_char msg[64];
		sprintf(
			(char *)msg,
			"Failed to allocate audio sample: %u",
			file_id
		);
		ice_log(msg);
		
		stb_vorbis_close(stream);
		
		sample->length = 0;
		sample->rate   = 0;
		
		return 0;
	}
	
	short buffer[256];
	int   decoded;
	int   data_index = 0;
	
	do {
		decoded=stb_vorbis_get_samples_short_interleaved(
			stream,
			info.channels,
			buffer,
			256
		);
		
		for (
			int i=0;
			i<decoded*info.channels;
			i+=info.channels
		) {
			int pulse=32767;
			
			//Convert stereo down to mono
			for (int j=0; j<info.channels; j++) {
				pulse = (pulse+buffer[i+j]+32767)/2;
			}
			
			sample->data[data_index]=(unsigned char)(pulse/256);
			
			data_index++;
		}
	} while (decoded>0);
	
	stb_vorbis_close(stream);
	
	return sample_id;
}

void ice_audio_sample_delete(
	ice_uint sample_id
) {
	if (
		samples==NULL ||
		sample_id>=MAX_SAMPLES ||
		samples[sample_id].data==NULL
	) {
		return;
	}
	
	audio_sample *sample=&samples[sample_id];
	
	free(sample->data);
	
	sample->data   = NULL;
	sample->rate   = 0;
	sample->length = 0;
}

ice_real ice_audio_sample_length_get(
	ice_uint sample_id
) {
	if (
		samples==NULL ||
		sample_id>=MAX_SAMPLES ||
		samples[sample_id].data==NULL
	) {
		return 0;
	}

	audio_sample *sample=&samples[sample_id];

	return (ice_real)sample->length/sample->rate;
}

ice_uint ice_audio_source_new() {
	if (sources==NULL) {
		return 0;
	}

	for (unsigned int i=0; i<MAX_SOURCES; i++) {
		if (sources[i].state==ICE_AUDIO_STATE_NONE) {
			audio_source *source=&sources[i];

			source->state     = ICE_AUDIO_STATE_PAUSED;
			source->sample_id = 0;
			source->position  = 0;
			source->volume    = 255;

			return (ice_uint)i;
		}
	}

	return 0;
}

void ice_audio_source_delete(
	ice_uint source_id
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return;
	}

	audio_source *source=&sources[source_id];

	source->state     = ICE_AUDIO_STATE_NONE;
	source->sample_id = 0;
	source->position  = 0;
	source->volume    = 255;
}

ice_uint ice_audio_source_sample_get(
	ice_uint source_id
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return 0;
	}

	return (ice_uint)sources[source_id].sample_id;
}

void ice_audio_source_sample_set(
	ice_uint source_id,
	ice_uint sample_id
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sample_id>=MAX_SAMPLES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return;
	}

	audio_source *source=&sources[source_id];

	source->sample_id = sample_id;
	source->position  = 0;
}

ice_real ice_audio_source_position_get(
	ice_uint source_id
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return 0;
	}

	audio_source *source=&sources[source_id];

	ice_uint sample_id   = source->sample_id;
	audio_sample *sample = &samples[sample_id];

	if (
		sample_id>=MAX_SAMPLES ||
		sample->data==NULL
	) {
		return 0;
	}

	return (ice_real)sources[source_id].position/sample->rate;
}

void ice_audio_source_position_set(
	ice_uint source_id,
	ice_real position
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return;
	}

	audio_source *source=&sources[source_id];

	ice_uint sample_id   = source->sample_id;
	audio_sample *sample = &samples[sample_id];

	if (
		sample_id>=MAX_SAMPLES ||
		sample->data==NULL
	) {
		return;
	}

	ice_uint sample_position = (ice_uint)(
		position*(ice_real)sample->rate
	);

	source->position=MAX(
		sample_position,
		sample->length
	);
}

ice_uint ice_audio_source_state_get(
	ice_uint source_id
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return ICE_AUDIO_STATE_PAUSED;
	}

	return (ice_uint)sources[source_id].state;
}

void ice_audio_source_state_set(
	ice_uint source_id,
	ice_uint state
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return;
	}

	audio_source *source=&sources[source_id];

	switch(state) {
		case ICE_AUDIO_STATE_PLAYING:
			source->state=ICE_AUDIO_STATE_PLAYING;
			
			break;
		case ICE_AUDIO_STATE_LOOPING:
			source->state=ICE_AUDIO_STATE_LOOPING;
			
			break;
		default:
			source->state=ICE_AUDIO_STATE_PAUSED;
	}
}

ice_real ice_audio_source_volume_get(
	ice_uint source_id
) {
   if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return 0;
	}

	return (ice_real)sources[source_id].volume/255;
}

void ice_audio_source_volume_set(
	ice_uint source_id,
	ice_real volume
) {
	if (
		sources==NULL ||
		source_id>=MAX_SOURCES ||
		sources[source_id].state==ICE_AUDIO_STATE_NONE
	) {
		return;
	}

	volume = MAX(volume,0);
	volume = MIN(volume,1);

	sources[source_id].volume=(ice_char)(volume*255);
}

ice_uint ice_audio_stream_load(
	ice_uint file_id
) {
	return 0;
}

void ice_audio_stream_delete(
	ice_uint stream_id
) {
}

ice_real ice_audio_stream_length_get(
	ice_uint stream_id
) {
	return 0;
}

ice_real ice_audio_stream_position_get(
	ice_uint stream_id
) {
	return 0;
}

void ice_audio_stream_position_set(
	ice_uint stream_id,
	ice_real position
) {
}

ice_uint ice_audio_stream_state_get(
	ice_uint stream_id
) {
	return 0;
}

void ice_audio_stream_state_set(
	ice_uint stream_id,
	ice_uint state
) {
}

ice_real ice_audio_stream_volume_get(
	ice_uint stream_id
) {
	return 0;
}

void ice_audio_stream_volume_set(
	ice_uint stream_id,
	ice_real volume
) {
}