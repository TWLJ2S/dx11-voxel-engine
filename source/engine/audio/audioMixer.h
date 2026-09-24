#pragma once

#include <SDL3/SDL.h>
#include <assets/cpuAsset.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
	int stb_vorbis_decode_filename(
		const char* filename,
		int* channels,
		int* sample_rate,
		short** output
	);
}

namespace ac {

	struct soundClip {
		std::vector<float> samples;
		int channels = 1;
		int sampleRate = 48000;
	};

	enum class soundMaterial : uint32_t {
		stone = 0,
		wood,
		grass,
		gravel,
		sand,
		snow,
		cloth,
		glass,
		count
	};

	enum class soundId : uint32_t {
		uiClick = 0,
		breakBlock,
		placeBlock,
		door,
		doorClose,
		chestOpen,
		chestClose,
		footstep,
		hurt,
		fallSmall,
		fallBig,
		swim,
		splash,
		count
	};

	enum class weatherSound : uint32_t {
		wind = 0,
		rain,
		snow,
		thunder
	};

	struct soundVoiceHandle {
		uint32_t index = UINT32_MAX;
		uint32_t generation = 0;

		explicit operator bool() const { return index != UINT32_MAX; }
	};

	struct soundPos {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float maxDistance = 16.0f;

		static soundPos at(float x, float y, float z, float maxDistance = 16.0f) {
			return { x, y, z, maxDistance };
		}

		static soundPos block(int x, int y, int z, float maxDistance = 16.0f) {
			return {
				static_cast<float>(x) + 0.5f,
				static_cast<float>(y) + 0.5f,
				static_cast<float>(z) + 0.5f,
				maxDistance
			};
		}
	};

	class audioMixer {
	private:
		struct voice {
			const soundClip* clip = nullptr;
			size_t cursor = 0;
			float volume = 1.0f;
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			float maxDistance = 16.0f;
			bool loop = false;
			bool active = false;
			bool spatial = false;
			uint32_t generation = 0;
		};

		struct listenerPose {
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			float fx = 0.0f;
			float fy = 0.0f;
			float fz = 1.0f;
			float ux = 0.0f;
			float uy = 1.0f;
			float uz = 0.0f;
		};

		SDL_AudioStream* _stream = nullptr;
		SDL_AudioDeviceID _device = 0;
		std::mutex _mutex;
		std::vector<voice> _voices;
		std::unordered_map<soundId, soundClip> _bank;
		std::array<std::vector<soundClip>, static_cast<size_t>(soundMaterial::count)> _dig;
		std::array<std::vector<soundClip>, static_cast<size_t>(soundMaterial::count)> _step;
		std::vector<soundClip> _doorOpen;
		std::vector<soundClip> _doorClose;
		std::vector<soundClip> _hurt;
		std::vector<soundClip> _fallSmall;
		std::vector<soundClip> _fallBig;
		std::vector<soundClip> _swim;
		std::vector<soundClip> _splash;
		std::vector<soundClip> _weatherRain;
		std::vector<soundClip> _weatherThunder;
		soundClip _weatherWind;
		soundClip _weatherSnow;
		float _masterVolume = 0.55f;
		listenerPose _listener{};
		bool _ready = false;
		std::mt19937 _rng{ 0xA11D10u };

		static soundClip synthesizeTone(
			float frequency,
			float durationSeconds,
			float volume,
			bool noise = false
		) {
			soundClip clip;
			clip.sampleRate = 48000;
			clip.channels = 1;
			const size_t count = static_cast<size_t>(durationSeconds * clip.sampleRate);
			clip.samples.resize(count);
			for (size_t i = 0; i < count; ++i) {
				const float t = static_cast<float>(i) / static_cast<float>(clip.sampleRate);
				const float envelope = std::exp(-t * 6.0f);
				float sample = 0.0f;
				if (noise) {
					sample = (static_cast<float>(rand() % 1000) / 500.0f - 1.0f) * envelope;
				}
				else {
					sample = std::sin(6.2831853f * frequency * t) * envelope;
				}
				clip.samples[i] = sample * volume;
			}
			return clip;
		}

		static soundClip synthesizeWind(float brightness) {
			soundClip clip;
			clip.sampleRate = 48000;
			clip.channels = 1;
			constexpr size_t sampleCount = 4u * 48000u;
			clip.samples.resize(sampleCount);
			std::vector<float> noise(sampleCount);
			uint32_t state = 0x57A7E12Du ^ static_cast<uint32_t>(brightness * 1000.0f);
			for (float& sample : noise) {
				state = state * 1664525u + 1013904223u;
				sample = static_cast<float>((state >> 8) & 0x00ffffffu) / 8388607.5f - 1.0f;
			}
			const int radius = std::clamp(static_cast<int>(90.0f - brightness * 62.0f), 18, 90);
			float filtered = 0.0f;
			for (int tap = -radius; tap <= radius; ++tap) {
				const int64_t wrapped =
					(static_cast<int64_t>(tap) + static_cast<int64_t>(sampleCount)) %
					static_cast<int64_t>(sampleCount);
				filtered += noise[static_cast<size_t>(wrapped)];
			}
			for (size_t i = 0; i < sampleCount; ++i) {
				const float t = static_cast<float>(i) / 48000.0f;
				const float gust = 0.58f + 0.24f * std::sin(6.2831853f * t * 0.25f) +
					0.18f * std::sin(6.2831853f * t * 0.75f + 1.7f);
				clip.samples[i] = filtered / static_cast<float>(radius * 2 + 1) *
					gust * (0.75f + brightness * 1.35f);
				const size_t removeAt =
					(i + sampleCount - static_cast<size_t>(radius)) % sampleCount;
				const size_t addAt =
					(i + static_cast<size_t>(radius) + 1u) % sampleCount;
				filtered += noise[addAt] - noise[removeAt];
			}
			return clip;
		}

		static soundClip loadWavFile(const std::string& path) {
			soundClip clip;
			Uint8* data = nullptr;
			Uint32 length = 0;
			SDL_AudioSpec spec{};
			if (!SDL_LoadWAV(path.c_str(), &spec, &data, &length) || !data || length == 0)
				return clip;

			SDL_AudioSpec target{};
			target.freq = 48000;
			target.format = SDL_AUDIO_F32;
			target.channels = 1;
			Uint8* converted = nullptr;
			int convertedLength = 0;
			if (!SDL_ConvertAudioSamples(
					&spec, data, static_cast<int>(length),
					&target, &converted, &convertedLength) ||
				!converted) {
				SDL_free(data);
				return clip;
			}

			clip.sampleRate = 48000;
			clip.channels = 1;
			const size_t samples = static_cast<size_t>(convertedLength) / sizeof(float);
			clip.samples.resize(samples);
			std::memcpy(clip.samples.data(), converted, convertedLength);
			SDL_free(converted);
			SDL_free(data);
			return clip;
		}

		static soundClip loadOggFile(const std::string& path) {
			soundClip clip;
			int channels = 0;
			int sampleRate = 0;
			short* pcm = nullptr;
			const int frames = stb_vorbis_decode_filename(
				path.c_str(), &channels, &sampleRate, &pcm);
			if (frames <= 0 || !pcm || channels <= 0) {
				if (pcm) free(pcm);
				return clip;
			}

			clip.sampleRate = 48000;
			clip.channels = 1;
			clip.samples.resize(static_cast<size_t>(frames));
			for (int i = 0; i < frames; ++i) {
				float sample = 0.0f;
				for (int c = 0; c < channels; ++c)
					sample += static_cast<float>(pcm[i * channels + c]) / 32768.0f;
				sample /= static_cast<float>(channels);
				clip.samples[static_cast<size_t>(i)] = sample;
			}
			free(pcm);

			if (sampleRate != 48000 && sampleRate > 0) {
				SDL_AudioSpec source{};
				source.freq = sampleRate;
				source.format = SDL_AUDIO_F32;
				source.channels = 1;
				SDL_AudioSpec target{};
				target.freq = 48000;
				target.format = SDL_AUDIO_F32;
				target.channels = 1;
				Uint8* converted = nullptr;
				int convertedLength = 0;
				if (SDL_ConvertAudioSamples(
						&source,
						reinterpret_cast<Uint8*>(clip.samples.data()),
						static_cast<int>(clip.samples.size() * sizeof(float)),
						&target,
						&converted,
						&convertedLength) &&
					converted) {
					const size_t samples = static_cast<size_t>(convertedLength) / sizeof(float);
					clip.samples.resize(samples);
					std::memcpy(clip.samples.data(), converted, convertedLength);
					SDL_free(converted);
				}
			}
			return clip;
		}

		static soundClip loadSoundFile(const std::string& path) {
			if (!std::filesystem::exists(path)) return {};
			const auto ext = std::filesystem::path(path).extension().string();
			if (ext == ".ogg" || ext == ".OGG")
				return loadOggFile(path);
			return loadWavFile(path);
		}

		static soundClip loadSoundStem(const std::string& stem) {
			soundClip clip = loadSoundFile(stem + ".ogg");
			if (clip.samples.empty())
				clip = loadSoundFile(stem + ".wav");
			return clip;
		}

		static void loadVariantFolder(
			std::vector<soundClip>& out,
			const std::string& directory,
			const std::string& prefix,
			int maxIndex
		) {
			for (int i = 1; i <= maxIndex; ++i) {
				soundClip clip = loadSoundStem(directory + "/" + prefix + std::to_string(i));
				if (!clip.samples.empty())
					out.push_back(std::move(clip));
			}
			if (out.empty()) {
				soundClip clip = loadSoundStem(directory + "/" + prefix);
				if (!clip.samples.empty())
					out.push_back(std::move(clip));
			}
		}

		static bool stemContainsAny(
			const std::string& stem,
			std::initializer_list<const char*> tokens
		) {
			for (const char* token : tokens) {
				if (token && stem.find(token) != std::string::npos)
					return true;
			}
			return false;
		}

		static void loadMatchingSounds(
			std::vector<soundClip>& out,
			const std::string& directory,
			std::initializer_list<const char*> tokens,
			bool recursive = false
		) {
			std::error_code error;
			if (!std::filesystem::exists(directory, error) || error)
				return;
			const auto consider = [&](const std::filesystem::path& path) {
				std::error_code fileError;
				if (!std::filesystem::is_regular_file(path, fileError) || fileError)
					return;
				if (!stemContainsAny(path.stem().string(), tokens))
					return;
				soundClip clip = loadSoundFile(path.string());
				if (!clip.samples.empty())
					out.push_back(std::move(clip));
			};
			if (recursive) {
				std::filesystem::recursive_directory_iterator it(
					directory,
					std::filesystem::directory_options::skip_permission_denied,
					error);
				const std::filesystem::recursive_directory_iterator end;
				while (!error && it != end) {
					consider(it->path());
					it.increment(error);
				}
				return;
			}
			std::filesystem::directory_iterator it(
				directory,
				std::filesystem::directory_options::skip_permission_denied,
				error);
			const std::filesystem::directory_iterator end;
			while (!error && it != end) {
				consider(it->path());
				it.increment(error);
			}
		}

		const soundClip* pick(const std::vector<soundClip>& clips) {
			if (clips.empty()) return nullptr;
			std::uniform_int_distribution<size_t> dist(0, clips.size() - 1);
			return &clips[dist(_rng)];
		}

		soundVoiceHandle playClip(
			const soundClip* clip,
			float volume,
			bool loop = false,
			const soundPos* pos = nullptr
		) {
			if (!_ready || !clip || clip->samples.empty()) return {};
			std::lock_guard lock(_mutex);
			for (size_t index = 0; index < _voices.size(); ++index) {
				voice& voice = _voices[index];
				if (voice.active) continue;
				voice.clip = clip;
				voice.cursor = 0;
				voice.volume = volume;
				voice.loop = loop;
				voice.active = true;
				++voice.generation;
				if (voice.generation == 0u) ++voice.generation;
				if (pos) {
					voice.spatial = true;
					voice.x = pos->x;
					voice.y = pos->y;
					voice.z = pos->z;
					voice.maxDistance = (std::max)(pos->maxDistance, 1.0f);
				}
				else {
					voice.spatial = false;
					voice.maxDistance = 16.0f;
				}
				return { static_cast<uint32_t>(index), voice.generation };
			}
			return {};
		}

		void stereoGains(const voice& voice, float& left, float& right) const {
			const float base = voice.volume * _masterVolume;
			if (!voice.spatial) {
				left = base;
				right = base;
				return;
			}

			const float dx = voice.x - _listener.x;
			const float dy = voice.y - _listener.y;
			const float dz = voice.z - _listener.z;
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (dist >= voice.maxDistance) {
				left = 0.0f;
				right = 0.0f;
				return;
			}

			const float atten = 1.0f - dist / voice.maxDistance;
			float fx = _listener.fx;
			float fy = _listener.fy;
			float fz = _listener.fz;
			const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
			if (fLen > 1.0e-5f) {
				fx /= fLen;
				fy /= fLen;
				fz /= fLen;
			}
			const float ux = _listener.ux;
			const float uy = _listener.uy;
			const float uz = _listener.uz;
			float rx = uy * fz - uz * fy;
			float ry = uz * fx - ux * fz;
			float rz = ux * fy - uy * fx;
			const float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
			if (rLen > 1.0e-5f) {
				rx /= rLen;
				ry /= rLen;
				rz /= rLen;
			}

			float pan = 0.0f;
			float front = 1.0f;
			if (dist > 1.0e-4f) {
				const float inv = 1.0f / dist;
				const float dirx = dx * inv;
				const float diry = dy * inv;
				const float dirz = dz * inv;
				pan = std::clamp(dirx * rx + diry * ry + dirz * rz, -1.0f, 1.0f);
				front = dirx * fx + diry * fy + dirz * fz;
			}
			const float rear = 0.70f + 0.30f * (front * 0.5f + 0.5f);
			const float gain = base * atten * rear;
			const float angle = (pan * 0.5f + 0.5f) * 1.57079637f;
			left = gain * std::cos(angle);
			right = gain * std::sin(angle);
		}

		std::array<bool, static_cast<size_t>(soundMaterial::count)> _digReady{};
		std::array<bool, static_cast<size_t>(soundMaterial::count)> _stepReady{};
		bool _doorsReady = false;
		bool _uiReady = false;
		bool _hurtReady = false;
		bool _waterReady = false;
		bool _weatherReady = false;

		void ensureUiSounds() {
			if (_uiReady) return;
			_uiReady = true;
			soundClip ui = loadSoundFile("assets/sounds/random/click.ogg");
			if (ui.samples.empty()) ui = loadSoundFile("assets/sounds/random/click_stereo.ogg");
			_bank[soundId::uiClick] = !ui.samples.empty()
				? std::move(ui)
				: synthesizeTone(880.0f, 0.05f, 0.35f);

			soundClip chestOpen = loadSoundFile("assets/sounds/random/chestopen.ogg");
			soundClip chestClose = loadSoundFile("assets/sounds/random/chestclosed.ogg");
			_bank[soundId::chestOpen] = !chestOpen.samples.empty()
				? std::move(chestOpen)
				: synthesizeTone(220.0f, 0.12f, 0.4f);
			_bank[soundId::chestClose] = !chestClose.samples.empty()
				? std::move(chestClose)
				: synthesizeTone(180.0f, 0.10f, 0.4f);
		}

		void ensureDoorSounds() {
			if (_doorsReady) return;
			_doorsReady = true;
			loadVariantFolder(_doorOpen, "assets/sounds/block/wooden_door", "open", 4);
			if (_doorOpen.empty()) {
				soundClip clip = loadSoundFile("assets/sounds/random/door_open.ogg");
				if (!clip.samples.empty()) _doorOpen.push_back(std::move(clip));
			}
			loadVariantFolder(_doorClose, "assets/sounds/block/wooden_door", "close", 6);
			if (_doorClose.empty()) {
				soundClip clip = loadSoundFile("assets/sounds/random/door_close.ogg");
				if (!clip.samples.empty()) _doorClose.push_back(std::move(clip));
			}
			if (_doorOpen.empty())
				_doorOpen.push_back(synthesizeTone(160.0f, 0.18f, 0.50f));
			if (_doorClose.empty())
				_doorClose.push_back(synthesizeTone(140.0f, 0.16f, 0.50f));
		}

		static const char* digPrefix(soundMaterial material) {
			switch (material) {
			case soundMaterial::wood: return "wood";
			case soundMaterial::grass: return "grass";
			case soundMaterial::gravel: return "gravel";
			case soundMaterial::sand: return "sand";
			case soundMaterial::snow: return "snow";
			case soundMaterial::cloth: return "cloth";
			case soundMaterial::glass: return "stone";
			default: return "stone";
			}
		}

		void ensureDig(soundMaterial material) {
			const size_t index = static_cast<size_t>(material);
			if (_digReady[index]) return;
			_digReady[index] = true;
			if (material == soundMaterial::glass) {
				soundClip glass = loadSoundFile("assets/sounds/random/glass1.ogg");
				if (glass.samples.empty()) glass = loadSoundFile("assets/sounds/random/glass2.ogg");
				if (!glass.samples.empty())
					_dig[index].push_back(std::move(glass));
				else
					ensureDig(soundMaterial::stone);
				return;
			}
			loadVariantFolder(_dig[index], "assets/sounds/dig", digPrefix(material), 4);
			if (_dig[index].empty())
				_dig[index].push_back(synthesizeTone(120.0f, 0.12f, 0.45f, true));
		}

		void ensureStep(soundMaterial material) {
			const size_t index = static_cast<size_t>(material);
			if (_stepReady[index]) return;
			_stepReady[index] = true;
			if (material == soundMaterial::glass) {
				ensureStep(soundMaterial::stone);
				_step[index] = _step[static_cast<size_t>(soundMaterial::stone)];
				return;
			}
			const int maxIndex = (material == soundMaterial::sand) ? 5
				: (material == soundMaterial::stone || material == soundMaterial::wood ||
					material == soundMaterial::grass) ? 6 : 4;
			loadVariantFolder(_step[index], "assets/sounds/step", digPrefix(material), maxIndex);
			if (_step[index].empty())
				_step[index].push_back(synthesizeTone(90.0f, 0.06f, 0.25f, true));
		}

		void ensureHurtSounds() {
			if (_hurtReady) return;
			_hurtReady = true;
			loadVariantFolder(_hurt, "assets/sounds/damage", "hit", 3);
			loadMatchingSounds(_hurt, "assets/sounds/damage", { "hit", "hurt" });
			loadMatchingSounds(_hurt, "assets/audio", { "hit", "hurt", "damage" }, true);
			if (_hurt.empty())
				_hurt.push_back(synthesizeTone(180.0f, 0.16f, 0.55f, true));

			soundClip fallSmall = loadSoundStem("assets/sounds/damage/fallsmall");
			if (fallSmall.samples.empty())
				loadMatchingSounds(_fallSmall, "assets/sounds/damage", { "fallsmall", "fall_small" });
			else
				_fallSmall.push_back(std::move(fallSmall));
			loadMatchingSounds(_fallSmall, "assets/audio", { "fallsmall", "fall_small" }, true);
			if (_fallSmall.empty())
				_fallSmall.push_back(synthesizeTone(140.0f, 0.18f, 0.50f, true));

			soundClip fallBig = loadSoundStem("assets/sounds/damage/fallbig");
			if (fallBig.samples.empty())
				loadMatchingSounds(_fallBig, "assets/sounds/damage", { "fallbig", "fall_big" });
			else
				_fallBig.push_back(std::move(fallBig));
			loadMatchingSounds(_fallBig, "assets/audio", { "fallbig", "fall_big" }, true);
			if (_fallBig.empty())
				_fallBig.push_back(synthesizeTone(90.0f, 0.28f, 0.65f, true));
		}

		void ensureWaterSounds() {
			if (_waterReady) return;
			_waterReady = true;
			loadVariantFolder(_swim, "assets/sounds/liquid", "swim", 18);
			loadMatchingSounds(_swim, "assets/sounds/liquid", { "swim", "water" });
			loadMatchingSounds(_swim, "assets/audio", { "swim", "water" }, true);
			if (_swim.empty())
				_swim.push_back(synthesizeTone(70.0f, 0.12f, 0.28f, true));

			loadVariantFolder(_splash, "assets/sounds/liquid", "splash", 2);
			soundClip heavy = loadSoundStem("assets/sounds/liquid/heavy_splash");
			if (!heavy.samples.empty())
				_splash.push_back(std::move(heavy));
			loadMatchingSounds(_splash, "assets/sounds/liquid", { "splash" });
			loadMatchingSounds(_splash, "assets/audio", { "splash" }, true);
			if (_splash.empty())
				_splash.push_back(synthesizeTone(110.0f, 0.22f, 0.45f, true));
		}

		void ensureWeatherSounds() {
			if (_weatherReady) return;
			_weatherReady = true;
			loadVariantFolder(
				_weatherRain, "assets/sounds/ambient/weather", "rain", 8);
			loadVariantFolder(
				_weatherThunder, "assets/sounds/ambient/weather", "thunder", 3);
			if (_weatherRain.empty())
				_weatherRain.push_back(synthesizeTone(95.0f, 2.0f, 0.18f, true));
			if (_weatherThunder.empty())
				_weatherThunder.push_back(synthesizeTone(42.0f, 1.8f, 0.75f, true));
			_weatherWind = synthesizeWind(0.28f);
			_weatherSnow = synthesizeWind(0.62f);
		}

		void mixInto(float* dst, int frames) {
			std::fill(dst, dst + frames * 2, 0.0f);
			std::lock_guard lock(_mutex);
			for (voice& voice : _voices) {
				if (!voice.active || !voice.clip || voice.clip->samples.empty())
					continue;
				float leftGain = 0.0f;
				float rightGain = 0.0f;
				stereoGains(voice, leftGain, rightGain);
				if (leftGain == 0.0f && rightGain == 0.0f && voice.spatial) {
					voice.cursor += static_cast<size_t>(frames);
					if (!voice.loop && voice.cursor >= voice.clip->samples.size())
						voice.active = false;
					else if (voice.loop && voice.clip->samples.size() > 0)
						voice.cursor %= voice.clip->samples.size();
					continue;
				}
				for (int i = 0; i < frames; ++i) {
					if (voice.cursor >= voice.clip->samples.size()) {
						if (voice.loop) {
							voice.cursor = 0;
						}
						else {
							voice.active = false;
							break;
						}
					}
					const float sample = voice.clip->samples[voice.cursor++];
					dst[i * 2 + 0] += sample * leftGain;
					dst[i * 2 + 1] += sample * rightGain;
				}
			}
			for (int i = 0; i < frames * 2; ++i)
				dst[i] = std::clamp(dst[i], -1.0f, 1.0f);
		}

		void playImpl(soundId id, float volume, bool loop, const soundPos* pos) {
			if (!_ready) return;
			if (id == soundId::door || id == soundId::doorClose) {
				ensureDoorSounds();
				playClip(pick(id == soundId::door ? _doorOpen : _doorClose), volume, loop, pos);
				return;
			}
			if (id == soundId::uiClick || id == soundId::chestOpen || id == soundId::chestClose)
				ensureUiSounds();
			else if (id == soundId::breakBlock || id == soundId::placeBlock) {
				ensureDig(soundMaterial::stone);
				playDigImpl(soundMaterial::stone, volume, pos);
				return;
			}
			else if (id == soundId::footstep) {
				ensureStep(soundMaterial::stone);
				playStepImpl(soundMaterial::stone, volume, pos);
				return;
			}
			else if (id == soundId::hurt) {
				playHurtImpl(volume, pos);
				return;
			}
			else if (id == soundId::fallSmall) {
				playFallImpl(false, volume, pos);
				return;
			}
			else if (id == soundId::fallBig) {
				playFallImpl(true, volume, pos);
				return;
			}
			else if (id == soundId::swim) {
				playSwimImpl(volume, pos);
				return;
			}
			else if (id == soundId::splash) {
				playSplashImpl(volume, pos);
				return;
			}
			auto found = _bank.find(id);
			if (found == _bank.end()) return;
			playClip(&found->second, volume, loop, pos);
		}

		void playDigImpl(soundMaterial material, float volume, const soundPos* pos) {
			if (!_ready) return;
			ensureDig(material);
			const size_t index = static_cast<size_t>(material);
			const soundClip* clip = pick(_dig[index]);
			if (!clip) {
				ensureDig(soundMaterial::stone);
				clip = pick(_dig[static_cast<size_t>(soundMaterial::stone)]);
			}
			playClip(clip, volume, false, pos);
		}

		void playStepImpl(soundMaterial material, float volume, const soundPos* pos) {
			if (!_ready) return;
			ensureStep(material);
			const size_t index = static_cast<size_t>(material);
			const soundClip* clip = pick(_step[index]);
			if (!clip) {
				ensureStep(soundMaterial::stone);
				clip = pick(_step[static_cast<size_t>(soundMaterial::stone)]);
			}
			playClip(clip, volume, false, pos);
		}

		void playHurtImpl(float volume, const soundPos* pos) {
			if (!_ready) return;
			ensureHurtSounds();
			playClip(pick(_hurt), volume, false, pos);
		}

		void playFallImpl(bool big, float volume, const soundPos* pos) {
			if (!_ready) return;
			ensureHurtSounds();
			playClip(pick(big ? _fallBig : _fallSmall), volume, false, pos);
		}

		void playSwimImpl(float volume, const soundPos* pos) {
			if (!_ready) return;
			ensureWaterSounds();
			playClip(pick(_swim), volume, false, pos);
		}

		void playSplashImpl(float volume, const soundPos* pos) {
			if (!_ready) return;
			ensureWaterSounds();
			playClip(pick(_splash), volume, false, pos);
		}

		static void SDLCALL streamCallback(
			void* userdata,
			SDL_AudioStream* stream,
			int additionalAmount,
			int /*totalAmount*/
		) {
			auto* self = static_cast<audioMixer*>(userdata);
			if (!self || !stream || additionalAmount <= 0)
				return;

			// Mix on SDL's audio thread so playback stays smooth even when
			// frame time spikes. Prefer full stereo frames for PutAudioStreamData.
			constexpr int bytesPerFrame = static_cast<int>(2 * sizeof(float));
			int remaining = additionalAmount;
			float buffer[1024 * 2];
			while (remaining > 0) {
				const int chunkBytes = std::min(remaining, static_cast<int>(sizeof(buffer)));
				int frames = chunkBytes / bytesPerFrame;
				if (frames <= 0)
					break;
				self->mixInto(buffer, frames);
				SDL_PutAudioStreamData(
					stream,
					buffer,
					frames * bytesPerFrame);
				remaining -= frames * bytesPerFrame;
			}
		}

	public:
		bool create() {
			SDL_AudioSpec want{};
			want.freq = 48000;
			want.format = SDL_AUDIO_F32;
			want.channels = 2;
			_stream = SDL_OpenAudioDeviceStream(
				SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
				&want,
				streamCallback,
				this);
			if (!_stream) return false;
			_device = SDL_GetAudioStreamDevice(_stream);
			_voices.resize(48);
			_ready = SDL_ResumeAudioDevice(_device);
			return _ready;
		}

		void destroy() {
			if (_stream) {
				SDL_SetAudioStreamGetCallback(_stream, nullptr, nullptr);
				SDL_DestroyAudioStream(_stream);
				_stream = nullptr;
			}
			_device = 0;
			_ready = false;
		}

		~audioMixer() { destroy(); }

		void setMasterVolume(float volume) {
			std::lock_guard lock(_mutex);
			_masterVolume = std::clamp(volume, 0.0f, 1.0f);
		}

		void setListener(
			float x, float y, float z,
			float fx, float fy, float fz,
			float ux = 0.0f, float uy = 1.0f, float uz = 0.0f
		) {
			std::lock_guard lock(_mutex);
			_listener = { x, y, z, fx, fy, fz, ux, uy, uz };
		}

		soundVoiceHandle playWeatherLoop(weatherSound sound, float volume = 0.0f) {
			if (!_ready || sound == weatherSound::thunder) return {};
			ensureWeatherSounds();
			const soundClip* clip = nullptr;
			switch (sound) {
			case weatherSound::rain: clip = pick(_weatherRain); break;
			case weatherSound::snow: clip = &_weatherSnow; break;
			default: clip = &_weatherWind; break;
			}
			return playClip(clip, volume, true, nullptr);
		}

		void playWeatherThunder(const soundPos& position, float volume = 1.0f) {
			if (!_ready) return;
			ensureWeatherSounds();
			playClip(pick(_weatherThunder), volume, false, &position);
		}

		bool setVoiceVolume(soundVoiceHandle handle, float volume) {
			std::lock_guard lock(_mutex);
			if (handle.index >= _voices.size()) return false;
			voice& selected = _voices[handle.index];
			if (!selected.active || selected.generation != handle.generation) return false;
			selected.volume = std::clamp(volume, 0.0f, 1.5f);
			return true;
		}

		void stopVoice(soundVoiceHandle& handle) {
			std::lock_guard lock(_mutex);
			if (handle.index < _voices.size()) {
				voice& selected = _voices[handle.index];
				if (selected.generation == handle.generation)
					selected.active = false;
			}
			handle = {};
		}

		void play(soundId id, float volume = 1.0f, bool loop = false) {
			playImpl(id, volume, loop, nullptr);
		}

		void play(soundId id, const soundPos& pos, float volume = 1.0f, bool loop = false) {
			playImpl(id, volume, loop, &pos);
		}

		void playDig(soundMaterial material, float volume = 1.0f) {
			playDigImpl(material, volume, nullptr);
		}

		void playDig(soundMaterial material, const soundPos& pos, float volume = 1.0f) {
			playDigImpl(material, volume, &pos);
		}

		void playPlace(soundMaterial material, float volume = 0.9f) {
			playDigImpl(material, volume, nullptr);
		}

		void playPlace(soundMaterial material, const soundPos& pos, float volume = 0.9f) {
			playDigImpl(material, volume, &pos);
		}

		void playStep(soundMaterial material, float volume = 0.55f) {
			playStepImpl(material, volume, nullptr);
		}

		void playStep(soundMaterial material, const soundPos& pos, float volume = 0.55f) {
			playStepImpl(material, volume, &pos);
		}

		void playHurt(float volume = 0.85f) {
			playHurtImpl(volume, nullptr);
		}

		void playHurt(const soundPos& pos, float volume = 0.85f) {
			playHurtImpl(volume, &pos);
		}

		void playFall(bool big, float volume = 0.90f) {
			playFallImpl(big, volume, nullptr);
		}

		void playFall(bool big, const soundPos& pos, float volume = 0.90f) {
			playFallImpl(big, volume, &pos);
		}

		void playSwim(float volume = 0.42f) {
			playSwimImpl(volume, nullptr);
		}

		void playSwim(const soundPos& pos, float volume = 0.42f) {
			playSwimImpl(volume, &pos);
		}

		void playSplash(float volume = 0.70f) {
			playSplashImpl(volume, nullptr);
		}

		void playSplash(const soundPos& pos, float volume = 0.70f) {
			playSplashImpl(volume, &pos);
		}

		// Mixing runs on the SDL audio callback thread; keep as a no-op for call sites.
		void update() {}

		bool ready() const { return _ready; }
	};

	inline soundMaterial soundMaterialForBlock(blockSoundMaterial material) {
		return static_cast<soundMaterial>(material);
	}

}
