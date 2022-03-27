CUDA_VER?= 11.4
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER is not set")
endif

APP:= bus_lane

CXX:= g++ -std=c++17

TARGET_DEVICE = $(shell g++ -dumpmachine | cut -f1 -d -)

NVDS_VERSION:=5.1

LIB_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/

ifeq ($(TARGET_DEVICE),aarch64)
  CFLAGS:= -DPLATFORM_TEGRA
endif

SRCS+= $(wildcard *.cpp)

INCS:= $(wildcard *.h)

PKGS:= gstreamer-1.0 opencv4

OBJS:= $(SRCS:.cpp=.o)

CFLAGS+= -I/opt/nvidia/deepstream/deepstream-5.1/sources/includes \
		 -DDS_VERSION_MINOR=0 -DDS_VERSION_MAJOR=5 \
		 -I /usr/local/cuda-$(CUDA_VER)/include

CFLAGS+= `pkg-config --cflags $(PKGS)`

LIBS:= `pkg-config --libs $(PKGS)`

LIBS+= -L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart \
		-L$(LIB_INSTALL_DIR) -lnvdsgst_meta -lnvds_meta \
		-lcuda -Wl,-rpath,$(LIB_INSTALL_DIR)

LIBS+=  -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_imgcodecs -pthread -lz -lssl

all: bus_lane objdets

objdets: yolov3 weights
bus_lane: $(APP)

%.o: %.cpp $(INCS) Makefile
	$(CXX) -c -o $@ $(CFLAGS) $<

$(APP): $(OBJS) Makefile
	$(CXX) -o $(APP) $(OBJS) $(LIBS)

yolov3:
	cd parsers/nvdsinfer_custom_impl_Yolo && $(MAKE)

weights:
	cd model && wget https://pjreddie.com/media/files/yolov3.weights -N -q --show-progress

clean:
	rm -rf $(OBJS) $(APP)
	cd parsers/nvdsinfer_custom_impl_Yolo && $(MAKE) clean
