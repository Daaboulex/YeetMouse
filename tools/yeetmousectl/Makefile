CXX = g++
CXXFLAGS = -O2 -Wall -Wno-sign-compare -std=c++17 -isystem ../../gui/External -MMD -MP -DTEST_ENV -I ../..
CFLAGS = -O2 -DTEST_ENV -funsigned-char -I ../.. -MMD -MP

SRC = main.cpp  \
      ../../gui/ConfigHelper.cpp \
      ../../gui/DriverHelper.cpp \
      ../../gui/CustomCurve.cpp \
      ../../gui/FunctionHelper.cpp \
      ../../gui/RawAccel.cpp \
      ../../gui/ImGuiStubs.cpp \
      ../../gui/Profiles.cpp

OBJ = $(SRC:.cpp=.o) driver_accel_modes.o driver_profile_table.o

driver_%.o: ../../driver/%.c
	$(CC) $(CFLAGS) -c $< -o $@

yeetmousectl: $(OBJ)
	$(CXX) -o yeetmousectl $(OBJ)

default: all
all: yeetmousectl

.PHONY: clean

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) yeetmousectl

-include $(OBJ:.o=.d)
