class PcmCapture extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sum = 0; this.count = 0; this.position = 0; this.offset = 0;
    this.buffer = new Int16Array(320);
  }
  process(inputs) {
    const channel = inputs[0]?.[0];
    if (!channel) return true;
    for (const sample of channel) {
      this.sum += sample; this.count++; this.position += 16000;
      if (this.position >= sampleRate) {
        this.position -= sampleRate;
        const value = Math.max(-1, Math.min(1, this.sum / this.count));
        this.buffer[this.offset++] = Math.round(value * (value < 0 ? 32768 : 32767));
        this.sum = 0; this.count = 0;
        if (this.offset === this.buffer.length) {
          this.port.postMessage(this.buffer.buffer, [this.buffer.buffer]);
          this.buffer = new Int16Array(320); this.offset = 0;
        }
      }
    }
    return true;
  }
}
registerProcessor('pcm-capture', PcmCapture);
