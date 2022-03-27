## DETECTION OF UNAUTHORISED VEHICLES IN BUS LANE
Bus lanes are supposed to be utilised solely by buses. But often, we see unauthorised vehicles utilizing these lanes, as a way to bypass traffic. This project demonstrates that we can use Deepstream and Computer Vision to detect such vehicles in a Video analytics pipeline.

## Installation of Dependencies
1. Installing System Dependencies for DeepStream
    ```sh
    sudo apt install \
    libssl1.0.0 \
    libgstreamer1.0-0 \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    libgstrtspserver-1.0-0 \
    libjansson4=2.11-1
    uuid \
    uuid-dev
    ```

2. DeepStream installation on Jetson Device
Download the deb file under - DeepStream 5.1 for Jetson from [here](https://developer.nvidia.com/deepstream-getting-started)

    ```sh
    sudo apt-get install ./deepstream-5.1_5.1.0-1_amd64.deb
    ```
    
2. Clone the repo
    ```sh
    git clone https://github.com/adlokib/bus_lane.git
    ```
3. Running the application

    1. Firstly we need to build the application
        ```sh
        make
        ```
    2. Put the absolute file path of the video source as an argument to the binary (The demonstration is using the sample.mp4 video as the ROI is defined for that particular video)
        ```sh
        ./bus_lane file:///home/adlokib/work/repos/bus_lane/vids/sample.mp4
        
        
## Find the link of the demo video below:
![Video](https://youtu.be/OoehrovGrls)
