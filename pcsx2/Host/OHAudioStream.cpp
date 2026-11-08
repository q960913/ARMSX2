// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// HarmonyOS / OpenHarmony audio output through OHAudio (OH_AudioRenderer).
// The renderer pulls float frames from the AudioStream ring on its own thread,
// the same pull model as the Oboe backend on Android.

#include "Host/AudioStream.h"

#include "common/Console.h"
#include "common/Error.h"

#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <algorithm>
#include <array>
#include <mutex>

namespace
{
	class OHAudioStream final : public AudioStream
	{
	public:
		OHAudioStream(u32 sample_rate, const AudioStreamParameters& parameters);
		~OHAudioStream() override;

		void SetPaused(bool paused) override;

		bool Initialize(bool stretch_enabled, Error* error);

	private:
		bool Open(Error* error);
		void Close();

		static int32_t OnWriteData(OH_AudioRenderer* renderer, void* user_data, void* buffer, int32_t length);
		static int32_t OnStreamEvent(OH_AudioRenderer* renderer, void* user_data, OH_AudioStream_Event event);
		static int32_t OnInterruptEvent(OH_AudioRenderer* renderer, void* user_data,
			OH_AudioInterrupt_ForceType type, OH_AudioInterrupt_Hint hint);
		static int32_t OnError(OH_AudioRenderer* renderer, void* user_data, OH_AudioStream_Result error);

		// Guards m_renderer against SetPaused() on the CPU thread racing Close().
		std::mutex m_lock;
		OH_AudioRenderer* m_renderer = nullptr;
		bool m_playing = false;
	};
} // namespace

OHAudioStream::OHAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
	: AudioStream(sample_rate, parameters)
{
}

OHAudioStream::~OHAudioStream()
{
	Close();
}

int32_t OHAudioStream::OnWriteData(OH_AudioRenderer* renderer, void* user_data, void* buffer, int32_t length)
{
	OHAudioStream* const self = static_cast<OHAudioStream*>(user_data);
	const u32 frame_bytes = self->m_output_channels * sizeof(SampleType);
	SampleType* out = static_cast<SampleType*>(buffer);

	// ReadFrames() allocas up to the request size, so keep each read bounded.
	for (u32 left = (out && length > 0) ? static_cast<u32>(length) / frame_bytes : 0; left > 0;)
	{
		const u32 frames = std::min<u32>(left, 2048);
		self->ReadFrames(out, frames);
		out += frames * self->m_output_channels;
		left -= frames;
	}
	return 0;
}

int32_t OHAudioStream::OnStreamEvent(OH_AudioRenderer* renderer, void* user_data, OH_AudioStream_Event event)
{
	return 0;
}

int32_t OHAudioStream::OnInterruptEvent(OH_AudioRenderer* renderer, void* user_data,
	OH_AudioInterrupt_ForceType type, OH_AudioInterrupt_Hint hint)
{
	// A call or another app took the audio focus. The system has already paused or
	// stopped the renderer; note it so the next resume starts it again.
	if (hint == AUDIOSTREAM_INTERRUPT_HINT_PAUSE || hint == AUDIOSTREAM_INTERRUPT_HINT_STOP)
	{
		OHAudioStream* const self = static_cast<OHAudioStream*>(user_data);
		std::lock_guard lock(self->m_lock);
		self->m_playing = false;
	}
	return 0;
}

int32_t OHAudioStream::OnError(OH_AudioRenderer* renderer, void* user_data, OH_AudioStream_Result error)
{
	Console.Error("(OHAudio) Renderer error %d", static_cast<int>(error));
	return 0;
}

bool OHAudioStream::Initialize(bool stretch_enabled, Error* error)
{
	static constexpr const std::array<SampleReader, static_cast<size_t>(AudioExpansionMode::Count)> sample_readers = {{
		&StereoSampleReaderImpl,
		&SampleReaderImpl<AudioExpansionMode::StereoLFE,
			READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_LFE>,
		&SampleReaderImpl<AudioExpansionMode::Quadraphonic,
			READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT,
			READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		&SampleReaderImpl<AudioExpansionMode::QuadraphonicLFE,
			READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_LFE,
			READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		&SampleReaderImpl<AudioExpansionMode::Surround51,
			READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_FRONT_CENTER,
			READ_CHANNEL_LFE, READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
		&SampleReaderImpl<AudioExpansionMode::Surround71,
			READ_CHANNEL_FRONT_LEFT, READ_CHANNEL_FRONT_RIGHT, READ_CHANNEL_FRONT_CENTER,
			READ_CHANNEL_LFE, READ_CHANNEL_SIDE_LEFT, READ_CHANNEL_SIDE_RIGHT,
			READ_CHANNEL_REAR_LEFT, READ_CHANNEL_REAR_RIGHT>,
	}};
	BaseInitialize(sample_readers[static_cast<size_t>(m_parameters.expansion_mode)], stretch_enabled);

	if (!Open(error))
		return false;

	std::lock_guard lock(m_lock);
	if (OH_AudioRenderer_Start(m_renderer) != AUDIOSTREAM_SUCCESS)
	{
		Error::SetStringView(error, "OH_AudioRenderer_Start() failed.");
		return false;
	}
	m_playing = true;
	return true;
}

bool OHAudioStream::Open(Error* error)
{
	OH_AudioStreamBuilder* builder = nullptr;
	if (OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER) != AUDIOSTREAM_SUCCESS || !builder)
	{
		Error::SetStringView(error, "OH_AudioStreamBuilder_Create() failed.");
		return false;
	}

	OH_AudioStreamBuilder_SetSamplingRate(builder, static_cast<int32_t>(m_sample_rate));
	OH_AudioStreamBuilder_SetChannelCount(builder, static_cast<int32_t>(m_output_channels));
	OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_F32LE);
	OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_GAME);
	OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);

	OH_AudioRenderer_Callbacks callbacks = {};
	callbacks.OH_AudioRenderer_OnWriteData = &OHAudioStream::OnWriteData;
	callbacks.OH_AudioRenderer_OnStreamEvent = &OHAudioStream::OnStreamEvent;
	callbacks.OH_AudioRenderer_OnInterruptEvent = &OHAudioStream::OnInterruptEvent;
	callbacks.OH_AudioRenderer_OnError = &OHAudioStream::OnError;
	OH_AudioStreamBuilder_SetRendererCallback(builder, callbacks, this);

	OH_AudioRenderer* renderer = nullptr;
	const OH_AudioStream_Result res = OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer);
	OH_AudioStreamBuilder_Destroy(builder);
	if (res != AUDIOSTREAM_SUCCESS || !renderer)
	{
		Error::SetStringFmt(error, "OH_AudioStreamBuilder_GenerateRenderer() failed: {}", static_cast<int>(res));
		return false;
	}

	std::lock_guard lock(m_lock);
	m_renderer = renderer;
	Console.WriteLn("(OHAudio) Renderer open: %u Hz, %u channels.", m_sample_rate, m_output_channels);
	return true;
}

void OHAudioStream::Close()
{
	std::lock_guard lock(m_lock);
	if (!m_renderer)
		return;

	if (m_playing)
		OH_AudioRenderer_Stop(m_renderer);
	OH_AudioRenderer_Release(m_renderer);
	m_renderer = nullptr;
	m_playing = false;
}

void OHAudioStream::SetPaused(bool paused)
{
	std::lock_guard lock(m_lock);
	if (m_paused == paused)
		return;

	if (m_renderer)
	{
		if (paused)
		{
			if (m_playing && OH_AudioRenderer_Pause(m_renderer) != AUDIOSTREAM_SUCCESS)
				OH_AudioRenderer_Stop(m_renderer);
			m_playing = false;
		}
		else if (!m_playing)
		{
			m_playing = (OH_AudioRenderer_Start(m_renderer) == AUDIOSTREAM_SUCCESS);
			if (!m_playing)
				Console.Error("(OHAudio) OH_AudioRenderer_Start() on resume failed.");
		}
	}
	m_paused = paused;
}

std::unique_ptr<AudioStream> AudioStream::CreateOHAudioStream(u32 sample_rate,
	const AudioStreamParameters& parameters, bool stretch_enabled, Error* error)
{
	std::unique_ptr<OHAudioStream> stream = std::make_unique<OHAudioStream>(sample_rate, parameters);
	if (!stream->Initialize(stretch_enabled, error))
		stream.reset();
	return stream;
}
