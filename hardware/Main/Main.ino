#include "HX711.h"
#include "Goldelox_Serial_4DLib.h"
#include "Goldelox_Const4D.h"
#include "Time.h"

#define CALIBRATION_CONSTANT_GRAMS 1090 //Found using LoadCellSetup
#define DisplaySerial Serial
/*
 * Load Cell Pins are:
 *    DOUT in 3
 *    SCK in 2
 * LCD Pins Are:
 *    TX into 0 (rx) Note: This must be unplugged for an upload to the arduino to work
 *    RX into 1 (tx)
 *    RES into 4 (include a 1 kilo-ohm resistor in this connection)
 * Button Pins are:
 *    SIGNAL PIN into 12
 */

//Hardware Globals
HX711 load_cell;
//use Serial0 to communicate with the display.
Goldelox_Serial_4DLib screen(&DisplaySerial);
const int button_pin = 12;

enum ScaleState {selMode, selRecipe, useRecipe, rawScale}; // Used to switch main loop behavior
enum GrindLevel {very_course, course, medium_course, fine, espresso_fine}; 

char * get_string(GrindLevel g) {
  switch(g) {
    case(very_course):
      return "Very Course";
    case(course):
      return "Course";
    case(medium_course):
      return "Medium Course";
    case(fine):
      return "Fine";
    case(espresso_fine):
      return "Espresso Fine";
  }
}

template <class T>
class Nameable { // Done so that this class can both be a wrapper, and an inheritable if you want to define it's behavior based on a private field
  protected:
    char* str;
    T* nonInherited;
  public:
    Nameable(char* s): str(s), nonInherited(NULL){};
    Nameable(char* s, T* n): str(s), nonInherited(n){};
    char* getName() {
      return str;
    }
    virtual T* get_underlying_type() {return nonInherited;}; 
};

class Step { // Parent class of all Steps to a Recipe
  protected:
    char * info;
  public:
    Step() : info("Default Text") {};
    Step(char * i) : info(i) {};
    virtual boolean add_to_display (Goldelox_Serial_4DLib * screen, double reading, int elapsed_seconds) { return true; }; //return true if step is finished
    virtual void clear_from_display (Goldelox_Serial_4DLib * screen) {
      screen->txt_MoveCursor(4, 0); //Make sure we move the position in whatever unit we were using before
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr("                                                                                "); // At least the length of a line haha
      reset_text(screen);
    }
};

class WeightDoneStep : public Step { // Step done when a certain weight is reached
  public:
    WeightDoneStep(int g, char * i): grams(g), Step(i) {}
    boolean add_to_display (Goldelox_Serial_4DLib * screen, double reading, int elapsed_seconds) {
      screen->txt_MoveCursor(4, 0); 
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr(Step::info);
      reset_text(screen);
      
      return (reading >= grams);
    }
  protected: 
    double grams;  
};

class TimeDoneStep : public Step { //Step done when a certain time is reached
  public:
    TimeDoneStep(int s, char * i): seconds(s), Step(i) {}
    boolean add_to_display (Goldelox_Serial_4DLib * screen, double reading, int elapsed_seconds) {
      screen->txt_MoveCursor(4, 0); 
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr(Step::info);
      screen->txt_MoveCursor(13,0);
      screen->print(elapsed_seconds);
      reset_text(screen);

      return (elapsed_seconds >= seconds);
    }
    void clear_from_display (Goldelox_Serial_4DLib * screen) {
      screen->txt_MoveCursor(4, 0); 
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr("                                                                                "); 
      screen->txt_MoveCursor(13,0);
      screen->putstr("                                                                                "); // Cover second line
      reset_text(screen);
    }
  protected:
    int seconds;
};

class WeightTimeDoneStep : public WeightDoneStep { //Step done when a certain weight and time are reached
  public:
    WeightTimeDoneStep(int g, int s, char * i): WeightDoneStep(g, i), seconds(s) {}
    boolean add_to_display (Goldelox_Serial_4DLib * screen, double reading, int seconds_elapsed) {
      screen->txt_MoveCursor(4, 0); //Make sure we move the position in whatever unit we were using before
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr(WeightDoneStep::info);
      
      screen->txt_MoveCursor(13, 0);

      double goal_rate = (WeightDoneStep::grams - reading) / (seconds - seconds_elapsed); // even grams per second required to reach goal in time left

      double cur_rate = reading / seconds_elapsed;

      if (cur_rate > (goal_rate + 4)) {
        screen->txt_FGcolour(RED);
        screen->putstr("Pour LESS!");
      }
      else if (cur_rate > (goal_rate + 2)) {
        screen->txt_FGcolour(YELLOW);
        screen->putstr("Pour less!");
      }
      else if (cur_rate < (goal_rate - 2)) {
        screen->txt_FGcolour(YELLOW);
        screen->putstr("Pour more!");
      }
      else if (cur_rate < (goal_rate - 4)) {
        screen->txt_FGcolour(RED);
        screen->putstr("Pour MORE!");
      }
      else {
        screen->putstr("SteadyPour");
      }

      screen->txt_MoveCursor(14,0);
      screen->print(seconds_elapsed);
      
      reset_text(screen);
      
      return (seconds_elapsed >= seconds) && (reading >= WeightDoneStep::grams);
    }
    void clear_from_display (Goldelox_Serial_4DLib * screen) {
      screen->txt_MoveCursor(4, 0); //Make sure we move the position in whatever unit we were using before
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr("                                                                                "); // At least the length of a line haha
      screen->txt_MoveCursor(13,0);
      screen->putstr("                                                                                "); // Cover second line
      screen->txt_MoveCursor(14,0);
      screen->putstr("                                                                                "); // Cover Third line
      reset_text(screen);
    }
    protected:
      int seconds;
};

class WeightTimeDoneMidStep: public WeightTimeDoneStep { //Step done when a certain weight and time are met, and the scale doesn't start at 0
  public:
    WeightTimeDoneMidStep(int g, int r, int s, char * i): WeightTimeDoneStep(g, s, i), starting_reading(r) {}
    boolean add_to_display (Goldelox_Serial_4DLib * screen, double reading, int seconds_elapsed) {
      screen->txt_MoveCursor(4, 0); //Make sure we move the position in whatever unit we were using before
      screen->txt_Height(1);
      screen->txt_Width(1);
      screen->putstr(WeightDoneStep::info);
      
      screen->txt_MoveCursor(13, 0);

      double goal_rate = (grams - (reading - starting_reading)) / (seconds - seconds_elapsed); // even grams per second required to reach goal in time left

      double cur_rate = (reading - starting_reading) / seconds_elapsed;

      if (cur_rate > (goal_rate + 4)) {
        screen->txt_FGcolour(RED);
        screen->putstr("Pour LESS!");
      }
      else if (cur_rate > (goal_rate + 2)) {
        screen->txt_FGcolour(YELLOW);
        screen->putstr("Pour less!");
      }
      else if (cur_rate < (goal_rate - 2)) {
        screen->txt_FGcolour(YELLOW);
        screen->putstr("Pour more!");
      }
      else if (cur_rate < (goal_rate - 4)) {
        screen->txt_FGcolour(RED);
        screen->putstr("Pour MORE!");
      }
      else {
        screen->putstr("SteadyPour");
      }

      screen->txt_MoveCursor(14,0);
      screen->print(seconds_elapsed);
      
      reset_text(screen);
      
      return (seconds_elapsed >= seconds) && (reading >= WeightDoneStep::grams);
    }
  private:
    int starting_reading;
};

class Recipe: public Nameable<Recipe> { //Collection of up to 10 steps into a recipe
  public:
    Recipe(char * n):currentStep(0), numSteps(0), Nameable(n) {}
    void add_to_display (Goldelox_Serial_4DLib * screen, double reading, int seconds_elapsed) {
      if (currentStep == numSteps) {
        screen->txt_MoveCursor(4, 0); //Make sure we move the position in whatever unit we were using before
        screen->txt_Height(1);
        screen->txt_Width(1);
        screen->putstr("Enjoy!");
        reset_text(screen);
      }
      else if (steps[currentStep]->add_to_display(screen, reading, seconds_elapsed)) {
        steps[currentStep]->clear_from_display(screen);
        if ( tareBetween[currentStep] == 1 ) {
          halt(screen);
        }
        currentStep++;
      }
    }
    void add_step(Step* s, bool tareAfter) {
      steps[numSteps] = s;
      if (tareAfter) {
        tareBetween[numSteps] = 1;
      }
      numSteps++;
    }
    int get_step() { return currentStep; }
    Recipe* get_underlying_type() {
      return this;
    }
  private:
    Step* steps[10]; //limiting at 10 steps right now
    int currentStep;
    int numSteps;
    int tareBetween[10]; //Same limit
};

template <class T>
class SelectionWheel {//Used to display wheel like selection mechanism
  public:
    SelectionWheel(): numOptions(0), curSelection(0), towardsChange(0) {};
    void add_option(Nameable<T>* option_pointer) {
      option_link[numOptions] = option_pointer;
      numOptions++;
    }
    void add_to_display(Goldelox_Serial_4DLib * screen, double reading) {

      //Because mod doesn't seem to actually work with negative values
      int fir = curSelection - 2;
      int sc = curSelection - 1;
      
      if (fir < 0) {
        fir += numOptions;
      }
      if (sc < 0) {
        sc += numOptions;
      }
            
      char* first = option_link[fir]->getName();
      char* sec = option_link[sc]->getName();
      char* third = option_link[curSelection]->getName(); // guaranteed to be within bounds
      char* fourth = option_link[(curSelection + 1) % numOptions]->getName();
      char* fifth = option_link[(curSelection + 2) % numOptions]->getName();

      screen->txt_Height(3);
      screen->txt_Width(3);  
      screen->txt_MoveCursor(1, 0); 
      screen->txt_Height(1);
      screen->txt_Width(1); 
      screen->putstr(first);

      screen->txt_Height(3);
      screen->txt_Width(3);
      screen->txt_MoveCursor(2, 0);
      screen->txt_Height(2);
      screen->txt_Width(2); 
      screen->putstr(sec);

      screen->txt_Height(3);
      screen->txt_Width(3);
      screen->txt_BGcolour(BROWN) ;
      screen->txt_MoveCursor(3, 0); 
      screen->putstr(third);
      screen->txt_BGcolour(BLACK) ;

      screen->txt_Height(3);
      screen->txt_Width(3);
      screen->txt_MoveCursor(4, 0);
      screen->txt_Height(2);
      screen->txt_Width(2); 
      screen->putstr(fourth);

      screen->txt_Height(3);
      screen->txt_Width(3);
      screen->txt_MoveCursor(5, 0); 
      screen->txt_Height(1);
      screen->txt_Width(1);      
      screen->putstr(fifth);

      towardsChange += reading;
      if ( towardsChange >= 50) {
        curSelection++;
        curSelection %= numOptions;
        towardsChange = 0;
      }
      reset_text(screen);
    }
    T* select() {
      return option_link[curSelection]->get_underlying_type();
    }
  private:
    Nameable<T>* option_link[99]; //arbitrary limit
    int numOptions;
    int curSelection;
    double towardsChange;
};


//Globals so that their pointers stay in scope
Recipe r1 = Recipe("V60          ");
Recipe r2 = Recipe("Chemex       ");
Recipe r3 = Recipe("Demo         ");
Recipe r4 = Recipe("Kenyan       ");
Recipe r5 = Recipe("Yirg         ");     

float last_reading = -10.0;
long last_checkpoint = 0;
int last_step = -1;

WeightDoneStep w = WeightDoneStep(20, "20g of Beans!");
WeightDoneStep w1 = WeightDoneStep(30, "30g Bloom!");
TimeDoneStep w2 = TimeDoneStep(20, "Wait 20s!");
WeightTimeDoneMidStep w3 = WeightTimeDoneMidStep(300, 30, 90, "300g Water for 1.5m!");  

WeightDoneStep s = WeightDoneStep(26, "26g of Beans!");
WeightTimeDoneStep s1 = WeightTimeDoneStep(50, 45, "50g Bloom for 45s!");
WeightTimeDoneMidStep s2 = WeightTimeDoneMidStep(150, 50, 45, "150g Water for 45s!"); 
WeightTimeDoneMidStep s3 = WeightTimeDoneMidStep(250, 150, 30, "250g Water for 30s!"); 
WeightTimeDoneMidStep s4 = WeightTimeDoneMidStep(400, 250, 30, "400g Water for 30s!"); 

WeightDoneStep d = WeightDoneStep(7, "7g of Beans!");
WeightDoneStep d1 = WeightDoneStep(14, "14g Bloom!");
TimeDoneStep d2 = TimeDoneStep(10, "Wait 10s!");
WeightTimeDoneMidStep d3 = WeightTimeDoneMidStep(100, 14, 60, "100g Water for 1m!");  

ScaleState state;
bool yesRecipe = true;
bool noRecipe = false;
Nameable<bool> yR = Nameable<bool>("Recipe!", &yesRecipe);
Nameable<bool> nR = Nameable<bool>("Scale! ", &noRecipe);
SelectionWheel<bool> mode_selector;
SelectionWheel<Recipe> recipe_selector;
Recipe* choice;

//set up text after fiddling's been done
void reset_text(Goldelox_Serial_4DLib * screen) {
  screen->txt_BGcolour(BLACK) ;
  screen->txt_FGcolour(GREEN) ;
  screen->txt_Height(3) ;
  screen->txt_Width(3) ;
}

//print scale numbers to the screen
float reading_to_screen (Goldelox_Serial_4DLib * screen) {
  //Basic Readout - Should be everpresent
  float reading = load_cell.get_units(2);
  if ((reading >= (last_reading + .05)) || (reading <= (last_reading -.05))){ //Make sure that the scale has a noticeable change before changing the display, to prevent flickering 
    char buffer[8];
    dtostrf(reading, 6, 2, buffer);
    screen->txt_MoveCursor(2, 0) ;
    screen->putstr(buffer);
    last_reading = reading;
  }
  return reading;
}

//Check button for pressing, and tare if pressed
void button_tare() {
  if(digitalRead(button_pin) == LOW) {
    load_cell.tare();
  }
}

//Pause program for button press
void halt(Goldelox_Serial_4DLib * screen){
  screen->txt_MoveCursor(4, 0); //Make sure we move the position in whatever unit we were using before
          screen->txt_Height(1);
          screen->txt_Width(1);
          screen->putstr("Press to continue!");
          reset_text(screen);
          while (digitalRead(button_pin) == HIGH) {
            reading_to_screen(screen);
          }
          load_cell.tare();
          screen->txt_Height(1);
          screen->txt_Width(1);
          screen->txt_MoveCursor(12, 0); //Make sure we move the position in whatever unit we were using before
          screen->putstr("                 ");
          reset_text(screen);
}


void setup() {
  // Set up Scale
  load_cell.begin(3, 2);
  load_cell.tare();
  load_cell.set_scale(CALIBRATION_CONSTANT_GRAMS);

  //For handling errors
  screen.Callback4D = mycallback ;  
  screen.TimeLimit4D = 5000;
  DisplaySerial.begin(9600);

  //Let Screen Start Up
  delay(2000);

  // Set up Style of screen
  screen.gfx_Cls();
  screen.gfx_ScreenMode(LANDSCAPE);
  screen.gfx_BGcolour(BLACK) ; 
  screen.SSTimeout(0) ;
  screen.SSSpeed(0) ;
  screen.SSMode(0) ;
  reset_text(&screen);
  screen.txt_Height(1) ;
  screen.txt_Width(1) ;
  screen.print("CaScale: Loading...");

  //Set up Recipes
  r1.add_step(&w, true);
  r1.add_step(&w1, false);
  r1.add_step(&w2, false);
  r1.add_step(&w3, false);

  r2.add_step(&s, true);
  r2.add_step(&s1, false);
  r2.add_step(&s2, false);
  r2.add_step(&s3, false);
  r2.add_step(&s4, false);

  r3.add_step(&d, true);
  r3.add_step(&d1, false);
  r3.add_step(&d2, false);
  r3.add_step(&d3, false);

  //Set up Scale Logic
  state = ScaleState(selMode);

  //Set up Selection Wheels
  mode_selector.add_option(&yR);
  mode_selector.add_option(&nR);
  
  recipe_selector.add_option(&r1);
  recipe_selector.add_option(&r2);
  recipe_selector.add_option(&r3);
  recipe_selector.add_option(&r4);
  recipe_selector.add_option(&r5);

  //Move to logic Loop
  delay(1500); // Make people FEEL like it's loading haha
  screen.gfx_Cls();
  reset_text(&screen);
}

void loop() {

  float reading;
  switch(state){
    case selMode: //Select Mode
      reading = load_cell.get_units(2);
      mode_selector.add_to_display(&screen, reading);
      if(digitalRead(button_pin) == LOW) {
        if (*mode_selector.select()){
          state = selRecipe;
          delay(500); // prevent double selection from long button press
        }
        else {
          state = rawScale;
        }
        screen.gfx_Cls();  
        reset_text(&screen);
      }
      break;
    case selRecipe: //Select Recipe
      reading = load_cell.get_units(2);
      recipe_selector.add_to_display(&screen, reading);
      if(digitalRead(button_pin) == LOW) {
        choice = recipe_selector.select();
        state = useRecipe;
        screen.gfx_Cls();
        reset_text(&screen);  
        halt(&screen);
      }
      break;
    case useRecipe: //Follow Recipe
      button_tare();

      reading = reading_to_screen(&screen);
  
      //Recipe Instructions - should change as needed
      if((choice->get_step() > last_step)) { // make sure each step has it's own timing bracket, and pouring's begun
        last_checkpoint = now(); 
        last_step = choice->get_step();
      }
  
      choice->add_to_display(&screen, reading, now() - last_checkpoint); 
      break;
    case rawScale: //Normal Scale
      button_tare();
      reading = reading_to_screen(&screen);
      break;
  }
  
}

void mycallback(int ErrCode, unsigned char Errorbyte) //Error callback for scale
{
  // Pin 13 has an LED connected on most Arduino boards. Just give it a name
  int led = 13;
  pinMode(led, OUTPUT);
  while(1)
  {
    digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(200);                // wait for 200 ms
    digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
    delay(200);                // wait for 200 ms
  }
}
