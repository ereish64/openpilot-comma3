import asyncio
import fractions
import logging
import threading
import time
from collections import deque

import numpy as np
from av import AudioFrame
from aiortc.mediastreams import AudioStreamTrack, MediaStreamError

from cereal import car, messaging

AUDIO_PTIME = 0.020
MIC_SAMPLE_RATE = 16000

AudibleAlert = car.CarControl.HUDControl.AudibleAlert
BODY_SOUND_ALERTS = {
  "engage": AudibleAlert.engage,
  "disengage": AudibleAlert.disengage,
  "prompt": AudibleAlert.prompt,
  "warning": AudibleAlert.warningImmediate,
}
BODY_SOUND_NAMES = frozenset(BODY_SOUND_ALERTS)


class PcmBuffer:
  def __init__(self, dtype=np.int16):
    self._chunks: deque[np.ndarray] = deque()
    self._offset = 0
    self._size = 0
    self._dtype = dtype

  def push(self, samples: np.ndarray):
    if samples.size == 0:
      return
    chunk = np.ascontiguousarray(samples, dtype=self._dtype)
    self._chunks.append(chunk)
    self._size += chunk.size

  def available(self) -> int:
    return self._size

  def pop(self, size: int) -> np.ndarray:
    out = np.zeros(size, dtype=self._dtype)
    written = 0

    while written < size and self._chunks:
      chunk = self._chunks[0]
      remaining = chunk.size - self._offset
      take = min(size - written, remaining)
      out[written:written + take] = chunk[self._offset:self._offset + take]
      written += take
      self._offset += take

      if self._offset >= chunk.size:
        self._chunks.popleft()
        self._offset = 0

    self._size -= written
    return out


class BodyMicAudioTrack(AudioStreamTrack):
  def __init__(self):
    import sounddevice as sd

    super().__init__()
    self.logger = logging.getLogger("webrtcd")
    self._loop = asyncio.get_running_loop()
    self._buffer = PcmBuffer()
    self._buffer_event = asyncio.Event()
    self._sample_rate = MIC_SAMPLE_RATE
    self._samples_per_frame = int(self._sample_rate * AUDIO_PTIME)
    self._lock = threading.Lock()
    self._sd = sd
    self._stream = self._sd.InputStream(
      channels=1,
      samplerate=self._sample_rate,
      callback=self._callback,
    )
    self._stream.start()

  def _callback(self, indata, frames, _time_info, status):
    if status:
      self.logger.warning("Mic input stream status: %s", status)

    pcm_samples = np.clip(indata[:, 0], -1.0, 1.0)
    pcm_int16 = (pcm_samples * 32767).astype(np.int16)

    def _push():
      with self._lock:
        self._buffer.push(pcm_int16)
      self._buffer_event.set()

    self._loop.call_soon_threadsafe(_push)

  async def recv(self):
    if self.readyState != "live":
      raise MediaStreamError

    while True:
      with self._lock:
        if self._buffer.available() >= self._samples_per_frame:
          frame_samples = self._buffer.pop(self._samples_per_frame)
          break
        self._buffer_event.clear()
      if self.readyState != "live":
        raise MediaStreamError
      await self._buffer_event.wait()

    if hasattr(self, "_timestamp"):
      self._timestamp += self._samples_per_frame
      wait = self._start + (self._timestamp / self._sample_rate) - time.time()
      await asyncio.sleep(wait)
    else:
      self._start = time.time()
      self._timestamp = 0

    frame = AudioFrame(format="s16", layout="mono", samples=self._samples_per_frame)
    frame.planes[0].update(frame_samples.tobytes())
    frame.pts = self._timestamp
    frame.sample_rate = self._sample_rate
    frame.time_base = fractions.Fraction(1, self._sample_rate)
    return frame

  def stop(self):
    super().stop()
    self._buffer_event.set()
    if self._stream is not None:
      self._stream.stop()
      self._stream.close()
      self._stream = None


class BodySpeaker:
  def __init__(self):
    self._pm = messaging.PubMaster(['soundRequest'])

  def play_sound(self, sound_name: str):
    msg = messaging.new_message('soundRequest')
    msg.soundRequest.sound = BODY_SOUND_ALERTS[sound_name]
    self._pm.send('soundRequest', msg)

  def start_track(self, track):
    pass

  async def stop(self):
    pass
