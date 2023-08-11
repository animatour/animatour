# AnimaTrip

## About

AnimaTrip is a low-latency multi-machine video system used for effortlessly compositing video sources, such as webcams, with video inputs and the composite video output delivered over the Internet.

AnimaTrip is based on UDP communication, the client-server model, C++17, and GStreamer.

AnimaTrip is heavily influenced by [JackTrip](https://github.com/jacktrip/jacktrip).

AnimaTrip is in the alpha phase. The client and the server applications are currently separate and run on Linux only.

## Architecture

```mermaid
graph LR
    subgraph server[Server]
        udp-receiver[UDP Receiver]-->udp-router[UDP Router]
        subgraph gst-composite[GStreamer Composite Pipeline]
            udpsrc-1[udpsrc 1]-->udpsrc-1-processing[rtph264depay/avdec_h264/videoscale/videoconvert]-->compositor
            udpsrc-2[udpsrc 2]-->udpsrc-2-processing[rtph264depay/avdec_h264/videoscale/videoconvert]-->compositor
            udpsrc-N[udpsrc N]-->udpsrc-N-processing[rtph264depay/avdec_h264/videoscale/videoconvert]-->compositor
            compositor-->compositor-processing[videobox/x264enc/rtph264pay]-->server-udpsink[udpsink]
        end
        udp-router-->udpsrc-1
        udp-router-->udpsrc-2
        udp-router-->udpsrc-N
        server-udpsink-->udp-sender[UDP Sender]
    end

    subgraph client-i[Client i]
        subgraph gst-capture[GStreamer Capture Pipeline]
            v4l2src-->capture-processing[videoconvert/videoscale/x264enc/rtph264pay]-->client-udpsink[udpsink]
        end
        subgraph gst-playback[GStreamer Playback Pipeline]
            client-udpsrc[udpsrc]-->playback-processing[rtph264depay/avdec_h264/videoconvert]
        end
        webcam[Webcam]-->v4l2src
        playback-processing-->display[Display]
    end

    udp-sender--UDP-->client-udpsrc
    udp-sender--UDP-->client-1[Client 1]
    udp-sender--UDP-->client-2[Client 2]
    udp-sender--UDP-->client-N[Client N]
    client-udpsink--UDP-->udp-receiver
    client-1--UDP-->udp-receiver
    client-2--UDP-->udp-receiver
    client-N--UDP-->udp-receiver
```

## Usage

### Run server

```bash
./server
```

### Run test client that connects to local server

```bash
./client test
```

You may run multiple test clients on a single machine.

### Run webcam client that connects to local server

```bash
./client /dev/video0
```

You may run multiple webcam clients on a single machine, but only one webcam client per webcam.
