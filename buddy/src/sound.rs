// His sounds to the speakers: Ruffle's mixer fed to the default output device by cpal (a trimmed copy of
// ruffle_frontend_utils' CpalAudioBackend, whose crate would bring a whole web client along)
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use ruffle_core::backend::audio::{
    AudioBackend, AudioMixer, DecodeError, RegisterError, SoundHandle, SoundInstanceHandle, SoundStreamInfo, SoundTransform, swf,
};
use ruffle_core::impl_audio_mixer_backend;

pub struct Speakers {
    stream: cpal::Stream,
    mixer: AudioMixer,
}

impl Speakers {
    // None: no output device (or one cpal can't use): he's silent
    pub fn new() -> Option<Self> {
        let device = cpal::default_host().default_output_device()?;
        let config = device.default_output_config().ok()?;
        let format = config.sample_format();
        let config = cpal::StreamConfig::from(config);
        let mixer = AudioMixer::new(config.channels as u8, config.sample_rate.0);
        let proxy = mixer.proxy();
        let stream = match format {
            cpal::SampleFormat::F32 => device.build_output_stream(&config, move |b, _| proxy.mix::<f32>(b), |_| {}, None),
            cpal::SampleFormat::I16 => device.build_output_stream(&config, move |b, _| proxy.mix::<i16>(b), |_| {}, None),
            _ => return None,
        }
        .ok()?;
        stream.play().ok()?;
        Some(Self { stream, mixer })
    }
}

impl AudioBackend for Speakers {
    impl_audio_mixer_backend!(mixer);

    fn play(&mut self) {
        self.stream.play().ok();
    }

    fn pause(&mut self) {
        self.stream.pause().ok();
    }
}

// Ruffle's mixer with no device: a test reads what he'd play (never out loud)
#[cfg(test)]
pub struct Unplugged {
    pub mixer: AudioMixer,
}

#[cfg(test)]
impl AudioBackend for Unplugged {
    impl_audio_mixer_backend!(mixer);

    fn play(&mut self) {}

    fn pause(&mut self) {}
}
