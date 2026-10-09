CXX = g++
CXXFLAGS = -O2 -Wall -Wno-sign-compare -std=c++17 -isystem ../../gui/External -MMD -MP -DTEST_ENV -I ../..
CFLAGS = -O2 -DTEST_ENV -funsigned-char -I ../.. -MMD -MP

GUI_SRC = ConfigHelper DriverHelper CustomCurve FunctionHelper RawAccel ImGuiStubs Profiles

OBJ = main.o $(GUI_SRC:%=gui_%.o) driver_accel_modes.o driver_profile_table.o

gui_%.o: ../../gui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
