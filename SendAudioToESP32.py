import asyncio
import wave
import time
import websockets

# কনফিগারেশন
WAV_FILE = "Two Steps From Hell.wav"
HOST = "0.0.0.0"  
PORT = 82

async def send_audio(websocket):
    print(f"ESP32 Connected from: {websocket.remote_address}")
    
    try:
        await websocket.send(WAV_FILE)
        # WAV ফাইলটি ওপেন করা হচ্ছে
        with wave.open(WAV_FILE, 'rb') as wf:
            sample_rate = wf.getframerate()      # সাধারণত 44100
            channels = wf.getnchannels()         # Stereo হলে 2, Mono হলে 1
            sampwidth = wf.getsampwidth()        # 16-bit এর জন্য 2 bytes
            
            # ১১০ms ডাটার জন্য স্যাম্পল সংখ্যা হিসাব (44100 * 0.045 = ~1984 samples)
            frame_duration = 0.045
            samples_per_frame = int(sample_rate * frame_duration)
            
            # প্রতি ফ্রেমে কত বাইট ডাটা থাকবে (samples * channels * bytes_per_sample)
            # Mono (16-bit)-এর জন্য: 1764 * 1 * 2 = 3969 Bytes
            bytes_per_frame = samples_per_frame * channels * sampwidth
            
            print(f"Audio Specs -> Sample Rate: {sample_rate}Hz, Channels: {channels}")
            print(f"Sending size: {bytes_per_frame} bytes per frame (~{frame_duration*1000}ms of data)")
            
            # প্রতি সেকেন্ডে ১০ বার পাঠানোর জন্য বিরতি (25 times per second = 40ms interval)
            send_interval = 0.042
            next_send = time.perf_counter()

            while True:
               
                # ফাইল থেকে নির্দিষ্ট সাইজের বাইনারি ডাটা রিড করা
                data = wf.readframes(samples_per_frame)
                
                # ফাইল শেষ হয়ে গেলে আবার প্রথম থেকে শুরু করার জন্য (Looping)
                if not data or len(data) < bytes_per_frame:
                    print("End of file reached. Restarting audio...")
                    wf.rewind()
                    data = wf.readframes(samples_per_frame)
                
                # ESP32-তে বাইনারি ডাটা পাঠানো
                await websocket.send(data)
                
                # ঠিক ১০০ms পর পর ডাটা পাঠানোর জন্য সময় হিসাব করা (Execution time বাদ দিয়ে)
                next_send += frame_duration
                delay = next_send - time.perf_counter()

                if delay > 0:
                    await asyncio.sleep(delay)
                else:
                    next_send = time.perf_counter()
            
                
                
    except websockets.exceptions.ConnectionClosedOK:
        print("ESP32 disconnected safely.")
    except websockets.exceptions.ConnectionClosedError as e:
        print(f"Connection closed with error: {e}")
    except FileNotFoundError:
        print(f"Error: {WAV_FILE} file not found in the current directory!")

async def main():
    print(f"WebSocket Server starting on ws://{HOST}:{PORT}")
    async with websockets.serve(send_audio, HOST, PORT):
        try:
            await asyncio.Future()  # সার্ভারটি অনবরত চালু রাখার জন্য
        except asyncio.CancelledError:
            pass

    

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nShutting down server...")
