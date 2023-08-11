# AnimaTrip

## About

AnimaTrip is a low-latency multi-machine video system used for effortlessly compositing video sources, such as webcams, with video inputs and the composite video output delivered over the Internet.

AnimaTrip is based on UDP communication, the client-server model, C++17, and GStreamer.

AnimaTrip is heavily influenced by [JackTrip](https://github.com/jacktrip/jacktrip).

AnimaTrip is in the alpha phase. The client and the server applications are currently separate and run on Linux only.

## Architecture

```mermaid
graph TD
    subgraph server[Server]
        udp-receiver[UDP Receiver]-->udp-router[UDP Router]
        subgraph gst-composite[GStreamer Composite Pipeline]
            udpsrc-1[udpsrc 1]-->udpsrc-1-rtph264depay[rtph264depay]-->udpsrc-1-avdec_h264[avdec_h264]-->udpsrc-1-videoscale[videoscale]-->udpsrc-1-videoconvert[videoconvert]-->compositor
            udpsrc-2[udpsrc 2]-->udpsrc-2-rtph264depay[rtph264depay]-->udpsrc-2-avdec_h264[avdec_h264]-->udpsrc-2-videoscale[videoscale]-->udpsrc-2-videoconvert[videoconvert]-->compositor
            udpsrc-N[udpsrc N]-->udpsrc-N-rtph264depay[rtph264depay]-->udpsrc-N-avdec_h264[avdec_h264]-->udpsrc-N-videoscale[videoscale]-->udpsrc-N-videoconvert[videoconvert]-->compositor
            compositor-->compositor-videobox[videobox]-->compositor-x264enc[x264enc]-->compositor-rtph264pay[rtph264pay]-->server-udpsink[udpsink]
        end
        udp-router-->udpsrc-1
        udp-router-->udpsrc-2
        udp-router-->udpsrc-N
        server-udpsink-->udp-sender[UDP Sender]
    end

    subgraph client-i[Client i]
        subgraph gst-capture[GStreamer Capture Pipeline]
            capture-v4l2src[v4l2src]-->capture-videoconvert[videoconvert]-->capture-videoscale[videoscale]-->capture-x264enc[x264enc]-->capture-rtph264pay[rtph264pay]-->capture-udpsink[udpsink]
        end
        subgraph gst-playback[GStreamer Playback Pipeline]
            playback-udpsrc[udpsrc]-->playback-rtph264depay[rtph264depay]-->playback-avdec_h264[avdec_h264]-->playback-videoconvert[videoconvert]
        end
        webcam[Webcam]-->capture-v4l2src
        playback-videoconvert-->display[Display]
    end

    udp-sender--UDP-->playback-udpsrc
    udp-sender--UDP-->client-1[Client 1]
    udp-sender--UDP-->client-2[Client 2]
    udp-sender--UDP-->client-N[Client N]
    capture-udpsink--UDP-->udp-receiver
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
