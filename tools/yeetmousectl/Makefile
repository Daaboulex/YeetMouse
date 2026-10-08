CXX = g++
CXXFLAGS = -O2 -Wall -Wno-sign-compare -std=c++17 -isystem ../../gui/External -MMD -MP

SRC = main.cpp  \
      ../../gui/ConfigHelper.cpp \
      ../../gui/DriverHelper.cpp \
      ../../gui/CustomCurve.cpp \
      ../../gui/FunctionHelper.cpp \
      ../../gui/RawAccel.cpp

OBJ = $(SRC:.cpp=.o)

yeetmousectl: $(OBJ)
	$(CXX) -o yeetmousectl $(OBJ)

default: all
all: yeetmousectl

.PHONY: clean

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) yeetmousectl

-include $(OBJ:.o=.d)
