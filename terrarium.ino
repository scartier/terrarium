// DONE
// * Change plant leaves to branches as it grows
// * Timed main loop - 48 ms at start - 31 ms typical
// * Merge gravity tile into dripper
// * Learned removing repeated calls helps code size 5594 vs 5728 for five Timer.set calls

// TODO
// * Change sense of neighbor water level to be how much it can accept
// * Require click before plant will grow
// * Bring back the bug!
// * Fix comm errors when waking from sleep

// WHEN HAVE MORE TILES
// * Ensure sunlight is absorbed/blocked by things correctly

#define null 0

#define INCLUDE_BASE_TILES    0
#define INCLUDE_SPECIAL_TILES 1

#define DEBUG_COMMS 0
#define DEBUG_COLORS 0
#define USE_DATA_SPONGE 0
#define NEW_RENDER 0
#define NEW_DRIPPER_RENDER 1

#define HUE_DRIPPER      106
#define HUE_DIRT          32
#define HUE_SUN           42
#define HUE_BUG           55
#define HUE_WATER        171
#define HUE_LEAF          85
#define HUE_BRANCH        28
#define HUE_FLOWER       233

//#define COLOR_BUG       makeColorHSB(HUE_BUG,    255, 128)
#define COLOR_LEAF      makeColorHSB(HUE_LEAF,   255, 128)
#define COLOR_BRANCH    makeColorHSB(HUE_BRANCH, 255, 128)
#define COLOR_WATER     makeColorHSB(HUE_WATER,  255, 128)

#if DEBUG_COLORS
#define COLOR_WATER_FULL  makeColorRGB(  0, 128, 128)
#define COLOR_WATER_FULL1  makeColorRGB( 64, 0, 0)
#define COLOR_WATER_FULL2  makeColorRGB(128, 0, 0)
#define COLOR_WATER_FULL3  makeColorRGB(255, 0, 0)
#define COLOR_WATER_FULL4  makeColorRGB(  0, 64, 0)
#define COLOR_WATER_FULL5  makeColorRGB(  0, 128, 0)
#define COLOR_WATER_FULL6  makeColorRGB(  0, 255, 0)
#define COLOR_WATER_FULL7  makeColorRGB(  0, 0, 64)
#define COLOR_WATER_FULL8  makeColorRGB(  0, 0, 128)
#define COLOR_WATER_FULL9  makeColorRGB(  0, 0, 255)
#endif

#if DEBUG_COLORS
#define COLOR_PLANT_GROWTH1 makeColorRGB(  128, 0,  64)
#define COLOR_PLANT_GROWTH2 makeColorRGB(  64, 0,  128)
#define COLOR_PLANT_GROWTH3 makeColorRGB(  64, 64,  64)
#endif

#define COLOR_DEBUG1  makeColorRGB(255, 255, 0) // yellow
#define COLOR_DEBUG2  makeColorRGB(255, 0, 255) // purple
#define COLOR_DEBUG3  makeColorRGB(0, 255, 255) // cyan

#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#define OPPOSITE_FACE(f) (((f) < 3) ? ((f) + 3) : ((f) - 3))
#define CW_FROM_FACE(f, amt) ((f + amt) % FACE_COUNT)
#define CCW_FROM_FACE(f, amt) (((f) - (amt)) + (((f) >= (amt)) ? 0 : 6))

// Gravity is a constant velocity for now (not accelerative)
#define GRAVITY 200 // ms to fall from pixel to pixel
Timer gravityTimer;

enum eTileFlags
{
  kTileFlag_PulseSun = 1<<0,
  kTileFlag_HasBug = 1<<1,
};
byte tileFlags;

unsigned long currentTime = 0;
byte frameTime;
byte worstFrameTime = 0;

enum eRenderBuffer
{
  kRenderBuffer_LerpFrom,
  kRenderBuffer_LerpTo,
  kRenderBuffer_LerpNext
};
byte renderHueOnFace[FACE_COUNT][3];
#define RENDER_TIMER_RATE 200
Timer renderTimer;

#if USE_DATA_SPONGE
byte sponge[143];
#endif

// =================================================================================================
//
// SYSTEMS
//
// =================================================================================================

// Roles the player can select for a tile
enum eTileRole
{
  kTileRole_Base,
  
  kTileRole_Dripper,
  kTileRole_Dirt,
  kTileRole_Sun,

  kTileRole_MAX
};

// Current tile state - starts empty
#if INCLUDE_BASE_TILES
char tileRole = kTileRole_Base;
#else
char tileRole = kTileRole_Dirt;
#endif

#define GRAVITY_UP_FACE_UNKNOWN -1
char gravityUpFace = 0;

#define SUN_SEEN_RATE 500
char sunSeen;
char sunBrightness;
Timer sunSeenTimer;

// -------------------------------------------------------------------------------------------------
// DRIPPER/WATER
//
#define MAX_WATER_LEVEL 15  // water level can actually go above this, but will stop accepting new water

#if INCLUDE_SPECIAL_TILES
#define DRIPPER_AMOUNT 4
#define DRIPPER_RATE 1000   // ms between drips
Timer dripperTimer;
byte dripperSpeedScale = 0;
#endif

#if INCLUDE_SPECIAL_TILES
#define DIRT_EVAPORATION 4
#endif

// Normal tiles and dirt tiles both evaporate water
#define EVAPORATION_RATE 5000
Timer evaporationTimer;

// -------------------------------------------------------------------------------------------------
// DIRT/PLANT
//
#if INCLUDE_SPECIAL_TILES
#define MAX_DIRT_RESERVOIR 100
#endif

// Most gathered sunlight a tile can hold
// The practical max will be a lot lower than this because leaves only collect 1 unit at a time
// Used by both normal tiles (plants) and dirt tiles
#define MAX_GATHERED_SUN 200

#if INCLUDE_SPECIAL_TILES
byte dirtReservoir = 0;
#endif

#if INCLUDE_BASE_TILES
byte gatheredSun = 0;   // used by plants (dirt tiles use the per-face 'gatheredSun')
#endif

// Amount of water that seeps out of a saturated dirt tile
#if INCLUDE_SPECIAL_TILES
#define DIRT_WATER_SEEP 4
#endif

// Used with plants. This is the rate that...
// ...collected sun is transmitted down the plant to the dirt
// ...dirt tiles distributes energy up to its plants
// ...plants distribute excess energy upwards to continue growth
#define PLANT_ENERGY_RATE 5000
Timer plantEnergyTimer;

#define MAX_ENERGY_PER_TILE 24
enum eBranchState
{
  kBranchState_Empty,
  kBranchState_Leaf,
  kBranchState_Branch,
  kBranchState_Flower
};
struct BranchState
{
  eBranchState state : 2;
  byte growthEnergy : 2;
  byte didGatherSun : 1;
};
struct PlantState
{
  byte rootFace : 3;
  byte energyTotal : 5;
  BranchState branches[3];
};
PlantState plantState;

#if INCLUDE_BASE_TILES
#define PLANT_FLOWER_RATE 60000   // blossom a flower after a minute in the right conditions
Timer plantFlowerTimer;
#endif

// -------------------------------------------------------------------------------------------------
// SUN
//
#if INCLUDE_SPECIAL_TILES
// Controls the rate at which sun tiles generate sunlight
#define SUN_STRENGTH 3
#define GENERATE_SUN_RATE 500
Timer generateSunTimer;
byte sunStrengthScale = 1;
#endif

//#if DEBUG_COLORS
#define SUN_PULSE_RATE 250
Timer sunPulseTimer;
//#endif

// -------------------------------------------------------------------------------------------------
// GRAVITY
//
// This timer is used by gravity tiles to broadcast the gravity direction
// It is also used by normal tiles to know not to accept any new gravity for a while
#define SEND_GRAVITY_RATE 5000
#define CHECK_GRAVITY_RATE (SEND_GRAVITY_RATE-1000)
Timer sendGravityTimer;

// -------------------------------------------------------------------------------------------------
// BUG
//
#define BUG_FLAP_RATE 100
Timer bugFlapTimer;
#define BUG_MOVE_RATE 7

byte bugTargetCorner  = 0;
char bugDistance      = 0;
char bugDirection     = 1;
byte bugFlapOpen      = 0;

// =================================================================================================
//
// COMMUNICATIONS
//
// =================================================================================================

#define TOGGLE_COMMAND 1
#define TOGGLE_DATA 0
struct FaceValue
{
  byte value  : 4;
  byte toggle : 1;
  byte ack    : 1;
};

enum FaceFlag
{
  FaceFlag_Present    = 1<<0,
  FaceFlag_GotWater   = 1<<1,
  FaceFlag_SentWater  = 1<<2,

  FaceFlag_Debug      = 1<<7
};

struct FaceState
{
  FaceValue faceValueIn;
  FaceValue faceValueOut;
  byte lastCommandIn;
  byte flags;
  
  byte waterLevel;
  byte waterLevelNew;
  byte waterLevelNeighbor;
  byte waterLevelNeighborNew;
  
#if INCLUDE_SPECIAL_TILES
  byte gatheredSun;           // so dirt tiles send energy to the right face
#endif

//  byte faceColor; // DEBUG

#if DEBUG_COMMS
  byte ourState;
  byte neighborState;
#endif
};
FaceState faceStates[FACE_COUNT];

#define NeighborSynced(f) ((faceStates[f].flags & \
                              (FaceFlag_Present | FaceFlag_GotWater)) == \
                              (FaceFlag_Present | FaceFlag_GotWater))

enum eTransferType
{
  kTransferType_Bug
};

enum CommandType
{
  CommandType_None,           // no data
  CommandType_WaterLevel,     // Our current water level
  CommandType_WaterAdd,       // Add Water To Neighbor : value=water level to add to the neighbor face
  CommandType_DistEnergy,     // Distribute Energy : value=energy
  CommandType_SendSun,        // Send sunlight (sent from Sun tiles): value=sun amount
  CommandType_SendSunCW,      // Send sunlight (sent from Sun tiles): value=sun amount (turns CW after one tile)
  CommandType_SendSunCCW,     // Send sunlight (sent from Sun tiles): value=sun amount (turns CCW after one tile)
  CommandType_GatherSun,      // Gather sun from leaves to root : value=sun amount
  CommandType_GravityDir,     // Tell neighbor which way gravity points - propagates out from gravity tile
  CommandType_TryTransfer,    // Attempt to transfer something from one tile to another (must be confirmed)
  CommandType_TryTransferCW,  // Same as above, but transfer to the face CW from the receiver
  CommandType_TransferAccept, // Confirmation of the transfer - sender must tolerate this never arriving, or arriving late
  CommandType_BlossomFlower,  // Plant is trying to blossom a flower at the end of a branch
#if DEBUG_COMMS
  CommandType_UpdateState,
#endif
  
  CommandType_MAX
};

struct CommandAndData
{
  CommandType command : 4;
  byte data : 4;
};

#define COMM_QUEUE_SIZE 4
CommandAndData commQueues[FACE_COUNT][COMM_QUEUE_SIZE];

#define COMM_INDEX_ERROR_OVERRUN 0xFF
#define COMM_INDEX_OUT_OF_SYNC   0xFE
#define COMM_DATA_OVERRUN        0xFD
byte commInsertionIndexes[FACE_COUNT];

#define ErrorOnFace(f) (commInsertionIndexes[f] > COMM_QUEUE_SIZE)


#if DEBUG_COMMS
// Timer used to toggle between green & blue
Timer sendNewStateTimer;
#endif


// =================================================================================================
//
// SETUP
//
// =================================================================================================

void setup()
{
#if USE_DATA_SPONGE
  // Use our data sponge so that it isn't compiled away
  if (sponge[0])
  {
    sponge[1] = 3;
  }
#endif
  
  currentTime = millis();
  
  resetOurState();
  FOREACH_FACE(f)
  {
    resetCommOnFace(f);
  }
  gravityTimer.set(GRAVITY);
}

// =================================================================================================
//
// COMMUNICATIONS
// Cut-and-paste from sample project
//
// =================================================================================================

void resetCommOnFace(byte f)
{
  // Clear the queue
  commInsertionIndexes[f] = 0;

  FaceState *faceState = &faceStates[f];

  // Put the current output into its reset state.
  // In this case, all zeroes works for us.
  // Assuming the neighbor is also reset, it means our ACK == their TOGGLE.
  // This allows the next pair to be sent immediately.
  // Also, since the toggle bit is set to TOGGLE_DATA, it will toggle into TOGGLE_COMMAND,
  // which is what we need to start sending a new pair.
  faceState->faceValueOut.value = 0;
  faceState->faceValueOut.toggle = TOGGLE_DATA;
  faceState->faceValueOut.ack = TOGGLE_DATA;
  sendValueOnFace(f, faceState->faceValueOut);

  // Clear sync flags dealing with our state
  faceState->flags &= ~FaceFlag_SentWater;
}

void sendValueOnFace(byte f, FaceValue faceValue)
{
  byte outVal = *((byte*)&faceValue);
  setValueSentOnFace(outVal, f);
}

// Called by the main program when this tile needs to tell something to
// a neighbor tile.
void enqueueCommOnFace(byte f, CommandType commandType, byte data, bool clampToMax)
{
  if (commInsertionIndexes[f] >= COMM_QUEUE_SIZE)
  {
    // Buffer overrun - might need to increase queue size to accommodate
    commInsertionIndexes[f] = COMM_INDEX_ERROR_OVERRUN;
    return;
  }

  if (data & 0xF0)
  {
    if (clampToMax)
    {
      data = 0xF;
    }
    else
    {
      commInsertionIndexes[f] = COMM_DATA_OVERRUN;
    }
  }
  
  byte index = commInsertionIndexes[f];
  commQueues[f][index].command = commandType;
  commQueues[f][index].data = data;
  commInsertionIndexes[f]++;
}

// Replace the data for the given command in the queue, if it exists.
// Otherwise add it to the queue.
void replaceOrEnqueueCommOnFace(byte f, CommandType commandType, byte data, bool clampToMax)
{
  for (byte index = 0; index < commInsertionIndexes[f]; index++)
  {
    if (commQueues[f][index].command == commandType)
    {
      if (data & 0xF0)
      {
        if (clampToMax)
        {
          data = 0xF;
        }
        else
        {
          commInsertionIndexes[f] = COMM_DATA_OVERRUN;
        }
      }

      commQueues[f][index].data = data;
      return;
    }
  }

  enqueueCommOnFace(f, commandType, data, clampToMax);
}

// Called every iteration of loop(), preferably before any main processing
// so that we can act on any new data being sent.
void updateCommOnFaces()
{
  FOREACH_FACE(f)
  {
    // Is the neighbor still there?
    if (isValueReceivedOnFaceExpired(f))
    {
      // Lost the neighbor - no longer in sync
      resetCommOnFace(f);
      faceStates[f].flags = 0;
      continue;
    }

    // If there is any kind of error on the face then do nothing
    // The error can be reset by removing the neighbor
    if (ErrorOnFace(f))
    {
      continue;
    }

    FaceState *faceState = &faceStates[f];

    faceState->flags |= FaceFlag_Present;

    // Read the neighbor's face value it is sending to us
    byte val = getLastValueReceivedOnFace(f);
    faceState->faceValueIn = *((FaceValue*)&val);
    
    //
    // RECEIVE
    //

    // Did the neighbor send a new comm?
    // Recognize this when their TOGGLE bit changed from the last value we got.
    if (faceState->faceValueOut.ack != faceState->faceValueIn.toggle)
    {
      // Got a new comm - process it
      byte value = faceState->faceValueIn.value;
      if (faceState->faceValueIn.toggle == TOGGLE_COMMAND)
      {
        // This is the first part of a comm (COMMAND)
        // Save the command value until we get the data
        faceState->lastCommandIn = value;
      }
      else
      {
        // This is the second part of a comm (DATA)
        // Do application-specific stuff with the comm
        processCommForFace(faceState->lastCommandIn, value, f);
      }

      // Acknowledge that we processed this value so the neighbor can send the next one
      faceState->faceValueOut.ack = faceState->faceValueIn.toggle;
    }
    
    //
    // SEND
    //
    
    // Did the neighbor acknowledge our last comm?
    // Recognize this when their ACK bit equals our current TOGGLE bit.
    if (faceState->faceValueIn.ack == faceState->faceValueOut.toggle)
    {
      // If we just sent the DATA half of the previous comm, check if there 
      // are any more commands to send.
      if (faceState->faceValueOut.toggle == TOGGLE_DATA)
      {
        if (commInsertionIndexes[f] == 0)
        {
          // Nope, no more comms to send - bail and wait
          continue;
        }
      }

      // Send the next value, either COMMAND or DATA depending on the toggle bit

      // Toggle between command and data
      faceState->faceValueOut.toggle = ~faceState->faceValueOut.toggle;
      
      // Grab the first element in the queue - we'll need it either way
      CommandAndData commandAndData = commQueues[f][0];

      // Send either the command or data depending on the toggle bit
      if (faceState->faceValueOut.toggle == TOGGLE_COMMAND)
      {
        faceState->faceValueOut.value = commandAndData.command;
      }
      else
      {
        faceState->faceValueOut.value = commandAndData.data;
  
        // No longer need this comm - shift everything towards the front of the queue
        for (byte commIndex = 1; commIndex < COMM_QUEUE_SIZE; commIndex++)
        {
          commQueues[f][commIndex-1] = commQueues[f][commIndex];
        }

        // Adjust the insertion index since we just shifted the queue
        if (commInsertionIndexes[f] == 0)
        {
          // Shouldn't get here - if so something is funky
          commInsertionIndexes[f] = COMM_INDEX_OUT_OF_SYNC;
          continue;
        }
        else
        {
          commInsertionIndexes[f]--;
        }
      }
    }
  }

  FOREACH_FACE(f)
  {
    // Update the value sent in case anything changed
    sendValueOnFace(f, faceStates[f].faceValueOut);
  }
}

// =================================================================================================
//
// LOOP
//
// =================================================================================================

void loop()
{
  // Keep track of time
  unsigned long previousTime = currentTime;
  currentTime = millis();

  // Clamp frame time at 255 ms to fit within a byte
  // Hopefully processing doesn't ever take longer than that for one frame
  unsigned long timeSinceLastLoop = currentTime - previousTime;
  frameTime = (timeSinceLastLoop > 255) ? 255 : (timeSinceLastLoop & 0xFF);
  if (frameTime > worstFrameTime)
  {
    worstFrameTime = frameTime;
  }
  
  // User input
  updateUserSelection();

  // Update neighbor presence and comms
  updateCommOnFaces();

#if DEBUG_COMMS
  if (sendNewStateTimer.isExpired())
  {
    FOREACH_FACE(f)
    {
      byte nextVal = faceStates[f].neighborState == 2 ? 3 : 2;
      faceStates[f].neighborState = nextVal;
      enqueueCommOnFace(f, CommandType_UpdateState, nextVal, true);
    }
    sendNewStateTimer.set(500);
  }
#else // DEBUG_COMMS

  // Systems updates
  switch (tileRole)
  {
#if INCLUDE_BASE_TILES
    case kTileRole_Base:
      loopWater();
      loopPlant();
      loopBug();
      break;
#endif
      
#if INCLUDE_SPECIAL_TILES
    case kTileRole_Dripper:  loopDripper();  loopGravity();  break;
    case kTileRole_Dirt:     loopDirt();     break;
    case kTileRole_Sun:      loopSun();      break;
#endif
  }  

  // Update water levels and such
  postProcessState();
  
#endif // DEBUG_COMMS

  // Set the colors on all faces according to what's happening in the tile
  render();
}

// =================================================================================================
//
// GENERAL
// Non-system-specific functions.
//
// =================================================================================================

void updateUserSelection()
{
  // Dirt tiles get reset too
  if (buttonMultiClicked())
  {
    // Reset our state and tell our neighbors to reset their perception of us
    resetOurState();

    // Give ourselves a bug!
    if (tileRole == kTileRole_Base)
    {
      tileFlags |= kTileFlag_HasBug;
    }
  }

#if INCLUDE_SPECIAL_TILES
  if (buttonDoubleClicked())
  {
    // User state changed

    char prevTileState = tileRole;
    
    tileRole++;
    if (tileRole >= kTileRole_MAX)
    {
#if INCLUDE_BASE_TILES
      tileRole = kTileRole_Base;
#else
      tileRole = kTileRole_Dripper;
#endif
    }

    // Reset our state and tell our neighbors to reset their perception of us
    resetOurState();

    if (prevTileState != tileRole)
    {
      switch (prevTileState)
      {
        case kTileRole_Dripper:
          break;
      }

      switch (tileRole)
      {
        case kTileRole_Dripper:
          dripperTimer.set(DRIPPER_RATE >> dripperSpeedScale);   // start dripping
          break;

        case kTileRole_Dirt:
          plantEnergyTimer.set(PLANT_ENERGY_RATE);
          break;
      }
    }
  }  // buttonDoubleClicked
#endif

  if (buttonSingleClicked() && !hasWoken())
  {
#if INCLUDE_SPECIAL_TILES
    switch (tileRole)
    {
      case kTileRole_Dripper:
        dripperSpeedScale++;
        if (dripperSpeedScale > 2)
        {
          dripperSpeedScale = 0;
        }
        break;
        
      case kTileRole_Sun:
        sunStrengthScale++;
        if (sunStrengthScale > 3)
        {
          sunStrengthScale = 1;
        }
        break;
    }
#endif
  }
}

void memclr(byte *ptr, byte len)
{
  byte *ptrEnd = ptr + len;
  while (ptr != ptrEnd)
  {
    *ptr = 0;
    ptr++;
  }
}

void resetOurState()
{
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];

    // Clear sync flags dealing with our state
    faceState->flags &= ~FaceFlag_SentWater;

    switch (tileRole)
    {
      case kTileRole_Base:
        faceState->waterLevel = 0;
        faceState->waterLevelNew = 0;

        memclr((byte*) &plantState, sizeof(plantState));
        break;

#if INCLUDE_SPECIAL_TILES
      case kTileRole_Dripper:
        dripperTimer.set(1);
        break;
        
      case kTileRole_Dirt:
        faceState->waterLevel = MAX_WATER_LEVEL;
        faceState->gatheredSun = 0;
        dirtReservoir = 0;
        break;

      case kTileRole_Sun:
        generateSunTimer.set(1);
        break;
#endif
    }

#if INCLUDE_BASE_TILES
    gatheredSun = 0;
#endif
  }

#if INCLUDE_BASE_TILES
  // Remove all plant energy - causes all plants to die immediately
  plantState.energyTotal = 0;
#endif

  tileFlags = 0;
  worstFrameTime = 0;
}

void processCommForFace(CommandType command, byte value, byte f)
{
  FaceState *faceState = &faceStates[f];

  // Use the saved command value to determine what to do with the data
  switch (command)
  {
    case CommandType_WaterLevel:
      faceState->waterLevelNeighbor = value;
      faceState->flags |= FaceFlag_GotWater;
      break;
      
    case CommandType_WaterAdd:
      switch (tileRole)
      {
#if INCLUDE_BASE_TILES
        case kTileRole_Base:
        {
          // Normal tiles accumulate water in each face
          faceState->waterLevel += value;
          replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceState->waterLevel, true);
        }
          break;
#endif

#if INCLUDE_SPECIAL_TILES
        case kTileRole_Dirt:
          // Dirt tiles have one large reservoir absorbed in the tile
          dirtReservoir += value;
          replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, getDirtWaterLevelToSend(), true);
          break;
#endif
      }
      break;
      
    case CommandType_DistEnergy:
#if INCLUDE_BASE_TILES
      if (tileRole == kTileRole_Base)
      {
        // Can only accept the energy if there is no plant growing here, or if
        // the energy is coming from the same root.
        if (plantState.branches[0].state == kBranchState_Empty || plantState.rootFace == f)
        {
          // Clamp to max amount we can store
          if ((MAX_ENERGY_PER_TILE - plantState.energyTotal) < value)
          {
            plantState.energyTotal = MAX_ENERGY_PER_TILE;
          }
          else
          {
            plantState.energyTotal += value;
          }
          plantState.rootFace = f;
        }
      }
#endif
      break;

    case CommandType_SendSun:
    case CommandType_SendSunCW:
    case CommandType_SendSunCCW:
      if (sunSeen < (255 - value))
      {
        sunSeen += value;
      }
#if INCLUDE_BASE_TILES
      if (tileRole == kTileRole_Base)
      {
        tileFlags |= kTileFlag_PulseSun;
        
        // Plant leaves absorb sunlight
        // Send on the rest in the same direction
        for (int branchIndex = 0; branchIndex < 3; branchIndex++)
        {
          if (value == 0)
          {
            break;    // break out once there's no more sun
          }
          if (plantState.branches[branchIndex].state == kBranchState_Leaf)
          {
            // Leaves absorb the sun
            plantState.branches[branchIndex].didGatherSun = 1;
            value--;
          }
          else if (plantState.branches[branchIndex].state == kBranchState_Branch)
          {
            // Branches just block the sun
            value--;
          }
        }
      }
#endif
#if INCLUDE_SPECIAL_TILES
      if (tileRole == kTileRole_Dirt)
      {
        // Dirt blocks all sunlight
        value = 0;
      }
      // Note: Drippers and sun tiles do not block sunlight
#endif

      // Any remaining sun gets sent on
      if (value > 0)
      {
        byte faceOffset = 3;  // typically we want the opposite face
        if (faceState->lastCommandIn == CommandType_SendSunCW)
        {
          faceOffset = 2;   // special case when sent out of a sun tile to make sunshine 3 hexes wide
        }
        else if (faceState->lastCommandIn == CommandType_SendSunCCW)
        {
          faceOffset = 4;   // special case when sent out of a sun tile to make sunshine 3 hexes wide
        }
        byte exitFace = CW_FROM_FACE(f, faceOffset);
        enqueueCommOnFace(exitFace, CommandType_SendSun, value, true);
      }
      break;
      
    case CommandType_GatherSun:
#if INCLUDE_BASE_TILES
      if (tileRole == kTileRole_Base)
      {
        // Plants propagate the sun until it gets to the root in the dirt
        if (plantState.branches[0].state != kBranchState_Empty)
        {
          if (gatheredSun < MAX_GATHERED_SUN)
          {
            gatheredSun += value;
          }

          // When receiving sun, convert our leaves to branches
          char faceOffsetFromRoot = plantState.rootFace - f;
          if (faceOffsetFromRoot == 2 || faceOffsetFromRoot == -4)
          {
            // Still growing - don't spawn a flower yet
            if (plantState.branches[2].state != kBranchState_Branch)
            {
              resetFlowerTimer();
            }
            plantState.branches[2].state = kBranchState_Branch;
          }
          else if (faceOffsetFromRoot == 4 || faceOffsetFromRoot == -2)
          {
            // Still growing - don't spawn a flower yet
            if (plantState.branches[1].state != kBranchState_Branch)
            {
              resetFlowerTimer();
            }
            plantState.branches[1].state = kBranchState_Branch;
          }
        }
      }
#endif
      
#if INCLUDE_SPECIAL_TILES
      if (tileRole == kTileRole_Dirt)
      {
        // Dirt tiles convert sun to energy!
        if (faceState->gatheredSun < MAX_GATHERED_SUN)
        {
          faceState->gatheredSun += value;
        }
      }
#endif
      break;

    case CommandType_GravityDir:
      if (tileRole != kTileRole_Dripper)    // dripper now sends out the gravity
      {
        if (sendGravityTimer.isExpired())
        {
          // Value is the CCW offset from this face
          gravityUpFace = CCW_FROM_FACE(f, value);
  
          // Propagate this on to our neighbors, except the one that just sent it
          propagateGravityDir(f);
  
          sendGravityTimer.set(CHECK_GRAVITY_RATE);
        }
      }
      break;

#if INCLUDE_BASE_TILES
    case CommandType_TryTransfer:
    case CommandType_TryTransferCW:
    {
      eTransferType transferType = value;
      switch (transferType)
      {
        case kTransferType_Bug:
          if (!(tileFlags & kTileFlag_HasBug))
          {
            // No bug in this tile - transfer accepted
            tileFlags |= kTileFlag_HasBug;
            bugTargetCorner = (command == CommandType_TryTransfer) ? f : CW_FROM_FACE(f, 1);
            bugDistance  = 64;
            bugDirection = -1;  // start going towards the middle
            bugFlapOpen = 0;    // looks better starting closed
            
            // Notify the sender
            enqueueCommOnFace(f, CommandType_TransferAccept, value, false);
          }
          break;
      }
    }
      break;

    case CommandType_TransferAccept:
    {
      eTransferType transferType = value;
      switch (transferType)
      {
        case kTransferType_Bug:
          // Bug transferred! Remove ours
          tileFlags &= ~kTileFlag_HasBug;
          break;
      }
    }
      break;
#endif

#if INCLUDE_BASE_TILES
    case CommandType_BlossomFlower:
      if (tileRole == kTileRole_Base)
      {
        // Can only grow a flower if there is no other plant growing in this tile
        if (plantState.branches[0].state == kBranchState_Empty)
        {
          plantState.rootFace = f;
          plantState.branches[0].state = kBranchState_Flower;

          // Spawn a bug, if we can!
          if (!(tileFlags & kTileFlag_HasBug))
          {
            tileFlags |= kTileFlag_HasBug;
            bugTargetCorner = MAX(5, currentTime & 0x7);
//            bugDistance = 0;
//            bugDirection = 1;
//            bugFlapOpen = 0;
          }
        }
      }
      break;
#endif
    
#if DEBUG_COMMS
    case CommandType_UpdateState:
      faceState->ourState = value;
      break;
#endif
  }
}

void postProcessState()
{
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];
    
    if (faceState->waterLevelNew > 0)
    {
      faceState->waterLevel += faceState->waterLevelNew;
      if (faceState->waterLevel > MAX_WATER_LEVEL)
      {
        faceState->waterLevel = MAX_WATER_LEVEL;
      }
      faceState->waterLevelNew = 0;

      if (NeighborSynced(f))
      {
        replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceState->waterLevel, true);
      }
    }

    if (faceState->waterLevelNeighborNew > 0)
    {
      if (NeighborSynced(f))
      {
        replaceOrEnqueueCommOnFace(f, CommandType_WaterAdd, faceState->waterLevelNeighborNew, true);
      }

      // Temporarily modify our understanding of the neighbor water level until they actually update us
      faceState->waterLevelNeighbor += faceState->waterLevelNeighborNew;
      faceState->waterLevelNeighborNew = 0;
    }

    // If we haven't sent our water level then do that now
    if (faceState->flags & FaceFlag_Present)
    {
      if (!(faceState->flags & FaceFlag_SentWater))
      {
        byte waterLevel = faceState->waterLevel;
        switch (tileRole)
        {
          case kTileRole_Base:
            // Empty tile uses actual water level
            break;

#if INCLUDE_SPECIAL_TILES
          case kTileRole_Dripper:
            // Drippers cannot accept water - it just falls around them
            waterLevel = MAX_WATER_LEVEL;
            break;

          case kTileRole_Sun:
            // Suns destroy water that falls in - it is always empty
            waterLevel = 0;
            break;
            
          case kTileRole_Dirt:
            // Dirt absorbs water over the entire tile
            waterLevel = getDirtWaterLevelToSend();
            break;
#endif
        }
        replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, waterLevel, true);
        faceState->flags |= FaceFlag_SentWater;
      }
    }
  }

  // Water evaporation/seepage
  if (evaporationTimer.isExpired())
  {
    switch (tileRole)
    {
#if INCLUDE_BASE_TILES
      case kTileRole_Base:
        byte amountToEvaporate = 1 + (sunSeen >> 1);
        FOREACH_FACE(f)
        {
          if (faceStates[f].waterLevel > 0)
          {
            if (faceStates[f].waterLevel > amountToEvaporate)
            {
              faceStates[f].waterLevel -= amountToEvaporate;
            }
            else
            {
              faceStates[f].waterLevel = 0;
            }
            
            replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceStates[f].waterLevel, true);
          }
        }
        break;
#endif

#if INCLUDE_SPECIAL_TILES
      case kTileRole_Dirt:
        if (dirtReservoir > 0)
        {
          dirtReservoir = (dirtReservoir < DIRT_EVAPORATION) ? 0 : (dirtReservoir - DIRT_EVAPORATION);
          byte waterLevel = getDirtWaterLevelToSend();
          FOREACH_FACE(f)
          {
            replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, waterLevel, true);
          }
        }
        break;
#endif
    }
    
    evaporationTimer.set(EVAPORATION_RATE);
  }
  
  if (gravityTimer.isExpired())
  {
    gravityTimer.set(GRAVITY);
  }
}

// =================================================================================================
//
// WATER/DRIPPER
//
// =================================================================================================

#if INCLUDE_SPECIAL_TILES
void loopDripper()
{
  if (dripperTimer.isExpired())
  {
    dripperTimer.set(DRIPPER_RATE >> dripperSpeedScale);

    // Send out water in all six directions, if there is room to accommodate
    FOREACH_FACE(f)
    {
      if (NeighborSynced(f))
      {
        byte amtToSend = DRIPPER_AMOUNT;
        
        // Temporarily reduce our water level to the amount we want to send out
        faceStates[f].waterLevel = amtToSend;
        // Don't care about the return value - the dripper doesn't care if water actually flowed
        byte amt = tryFlowWater(&faceStates[f].waterLevel, &faceStates[f].waterLevelNeighbor, &faceStates[f].waterLevelNeighborNew, amtToSend);

        // Restore our water level to "full"
        // Water should flow around us
        faceStates[f].waterLevel = MAX_WATER_LEVEL;
      }
    }
  }
}
#endif

#if INCLUDE_BASE_TILES
struct WaterFlowCommand
{
  byte srcFace : 3;
  byte dstFace : 3;
  byte isNeighbor : 1;
  byte halfWater : 1;
};

WaterFlowCommand waterFlowSequence[] =
{
  { 3, 3, 1, 0 },   // fall from face 3 down out of this tile
  { 0, 3, 0, 0 },   // top row falls into bottom row
  { 1, 2, 0, 0 },   // ...
  { 5, 4, 0, 0 },   // ...

  // When flowing to sides, every other command has 'halfWater' set
  // so that the first will send half and then the next will send the
  // rest (the other half).
  // This has a side effect "bug" where, if there is a left neighbor,
  // but not a right, it will only send half the water. While if there
  // is a right neighbor but not a left it will send it all.
  // Hope no one notices shhhhh
  { 5, 0, 0, 1 },   // flow to sides
  { 5, 5, 1, 0 },
  { 4, 3, 0, 1 },
  { 4, 4, 1, 0 },
  { 0, 5, 0, 1 },
  { 0, 1, 0, 0 },
  { 3, 4, 0, 1 },
  { 3, 2, 0, 0 },
  { 1, 0, 0, 1 },
  { 1, 1, 1, 0 },
  { 2, 3, 0, 1 },
  { 2, 2, 1, 0 }
};

void loopWater()
{
  // Make water fall/flow
  if (gravityTimer.isExpired())
  {
  // Water cannot flow until we know which way gravity points
    if (gravityUpFace == GRAVITY_UP_FACE_UNKNOWN)
    {
      return;
    }
    
    for (int waterFlowIndex = 0; waterFlowIndex < 16; waterFlowIndex++)
    {
      WaterFlowCommand command = waterFlowSequence[waterFlowIndex];

      byte srcFace = command.srcFace;
      byte dstFace = command.dstFace;

      // Factor in gravity
      // Faces increase CW around the tile - need the CCW value to offset
      byte gravityCCWAmount = 6 - gravityUpFace;
      srcFace = CCW_FROM_FACE(srcFace, gravityCCWAmount);
      dstFace = CCW_FROM_FACE(dstFace, gravityCCWAmount);
      
      FaceState *faceStateSrc = &faceStates[srcFace];
      FaceState *faceStateDst = &faceStates[dstFace];
      
      byte *dst = command.isNeighbor ? &faceStateDst->waterLevelNeighbor : &faceStateDst->waterLevel;
      byte *dstNew = command.isNeighbor ? &faceStateDst->waterLevelNeighborNew : &faceStateDst->waterLevelNew;
      byte amountToSend = faceStateSrc->waterLevel >> command.halfWater;
      amountToSend = MIN(amountToSend, MAX_WATER_LEVEL);
      
      if (!command.isNeighbor || NeighborSynced(dstFace))
      {
        if (tryFlowWater(&faceStateSrc->waterLevel, dst, dstNew, amountToSend) > 0)
        {
          replaceOrEnqueueCommOnFace(srcFace, CommandType_WaterLevel, faceStateSrc->waterLevel, true);
        }
      }
    }
  }
}
#endif

byte tryFlowWater(byte *srcWaterLevel, byte *dst, byte *dstNew, byte amountToSend)
{
  if (*srcWaterLevel <= 0)
  {
    return 0;   // no water here to fall
  }

  byte dstTotal = *dst + *dstNew;
  if (dstTotal >= MAX_WATER_LEVEL)
  {
    return 0;   // no room in destination to accept more water
  }

  // All water falls, if possible, up to the maximum the destination can hold
  int waterAmount = MIN(MAX_WATER_LEVEL - dstTotal, amountToSend);
  //waterAmount = MIN(waterAmount, maxAmount);
  
  // Move the water!
  *srcWaterLevel -= waterAmount;
  *dstNew += waterAmount;

  return waterAmount;
}

// =================================================================================================
//
// DIRT/PLANT
//
// =================================================================================================

#if INCLUDE_SPECIAL_TILES
void loopDirt()
{
  // Once saturated, dirt tiles will seep water
  // TODO : Seep more intuitively - down first (2,3,4) or to sides if down is full/blocked (1,5) - never up (0)
  if (gravityTimer.isExpired())
  {
    if (dirtReservoir >= MAX_DIRT_RESERVOIR)
    {
      if (NeighborSynced(2))
      {
        tryFlowWater(&dirtReservoir, &faceStates[2].waterLevelNeighbor, &faceStates[2].waterLevelNeighborNew, DIRT_WATER_SEEP);
      }
      if (NeighborSynced(3))
      {
        tryFlowWater(&dirtReservoir, &faceStates[3].waterLevelNeighbor, &faceStates[3].waterLevelNeighborNew, DIRT_WATER_SEEP);
      }
      if (NeighborSynced(4))
      {
        tryFlowWater(&dirtReservoir, &faceStates[4].waterLevelNeighbor, &faceStates[4].waterLevelNeighborNew, DIRT_WATER_SEEP);
      }
    }

    // If water seeped out, tell our neighbors that we can accept more
    if (dirtReservoir < MAX_DIRT_RESERVOIR)
    {
      byte waterLevel = getDirtWaterLevelToSend();
      FOREACH_FACE(f)
      {
        replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, waterLevel, true);
      }
    }
  }

  // Generate energy for plants!
  if (plantEnergyTimer.isExpired())
  {
    FOREACH_FACE(f)
    {
      if (NeighborSynced(f))
      {
        // Energy generation formula:
        // Up to 3 water = 3 energy (so that a plant can sprout without needing sunlight)
        // After that 1 water + 1 sun = 3 energy
        if (dirtReservoir > 0)
        {
          byte energyToDistribute = (dirtReservoir > 3) ? 3 : dirtReservoir;
          dirtReservoir -= energyToDistribute;
    
          byte minWaterOrSun = MIN(dirtReservoir, faceStates[f].gatheredSun);
          dirtReservoir -= minWaterOrSun;
          faceStates[f].gatheredSun = 0;
    
          energyToDistribute += minWaterOrSun * 3;

          if (energyToDistribute > 0)
          {
            enqueueCommOnFace(f, CommandType_DistEnergy, energyToDistribute, true);
          }
        }
      }
    }
    
    plantEnergyTimer.set(PLANT_ENERGY_RATE);
  }
}

byte getDirtWaterLevelToSend()
{
  byte waterLevel = 0;
  
  // Dirt absorbs water over the entire tile
  if (dirtReservoir >= MAX_DIRT_RESERVOIR)
  {
    // Tile has absorbed all the water it can
    waterLevel = MAX_WATER_LEVEL;
  }
  else if (MAX_DIRT_RESERVOIR - dirtReservoir >= MAX_WATER_LEVEL)
  {
    // Tile has room to accept the max level of water that can be sent
    waterLevel = 0;
  }
  else
  {
    // Somewhere in between
    waterLevel = MAX_DIRT_RESERVOIR - dirtReservoir;
  }

  return waterLevel;
}
#endif

#if INCLUDE_BASE_TILES
void loopPlant()
{
  // Plants use energy periodically to stay alive and grow
  if (plantEnergyTimer.isExpired())
  {
    //
    // Maintain grown branches first (energy use = 1 per branch)
    //
    for (int branchIndex = 0; branchIndex < 3; branchIndex++)
    {
      if (plantState.branches[branchIndex].state != kBranchState_Empty)
      {
        if (plantState.energyTotal > 0)
        {
          // There is enough energy to maintain this branch - replenish it and deduct the energy
          plantState.branches[branchIndex].growthEnergy = 3;
          plantState.energyTotal--;
        }
        else
        {
          // Not enough energy to maintain this branch - it is dying
          if (plantState.branches[branchIndex].growthEnergy > 0)
          {
            plantState.branches[branchIndex].growthEnergy--;
          }
          else
          {
            // No more growth energy - this branch is dead
            plantState.branches[branchIndex].state = kBranchState_Empty;
          }

          // Flower can't grow in these conditions!
          resetFlowerTimer();
        }
      }
    }

    // Maintain growing branches next (energy use = growth state per branch)
    for (int branchIndex = 0; branchIndex < 3; branchIndex++)
    {
      if (plantState.branches[branchIndex].state == kBranchState_Empty &&
          plantState.branches[branchIndex].growthEnergy > 0)
      {
        if (plantState.energyTotal >= plantState.branches[branchIndex].growthEnergy)
        {
          // There is enough energy to maintain this growth - deduct the energy
          plantState.energyTotal -= plantState.branches[branchIndex].growthEnergy;
        }
        else
        {
          // Not enough energy to maintain this branch - take a step back
          plantState.branches[branchIndex].growthEnergy--;
        }

        // Only try to grow one branch at a time
        // TODO : Randomize this somehow
        break;
      }
    }

    // Remaining energy goes into trying to grow new branches
    for (int branchIndex = 0; branchIndex < 3; branchIndex++)
    {
      if (plantState.branches[branchIndex].state == kBranchState_Empty)
      {
        // Growing requires energy of current level+1
        if (plantState.energyTotal > plantState.branches[branchIndex].growthEnergy)
        {
          plantState.energyTotal -= plantState.branches[branchIndex].growthEnergy + 1;
          plantState.branches[branchIndex].growthEnergy++;
        }

        // Once reach max growth level, sprout a leaf!
        if (plantState.branches[branchIndex].growthEnergy == 3)
        {
          // Still growing - don't spawn a flower yet
          resetFlowerTimer();

          plantState.branches[branchIndex].state = kBranchState_Leaf;

          // Leaves on the outer branches convert the root to a branch
          if (branchIndex > 0)
          {
            plantState.branches[0].state = kBranchState_Branch;
          }
        }

        // If the root branch is still growing, don't process the other branches
        if (branchIndex == 0)
        {
          break;
        }
      }
    }

    // Flowers can only blossom in the presences of sunlight
    if (sunSeen == 0)
    {
      resetFlowerTimer();
    }
    
    // Check about converting leaves into flowers
    if (plantFlowerTimer.isExpired())
    {
      resetFlowerTimer();
      for (int branchIndex = 1; branchIndex < 3; branchIndex++)
      {
        if (plantState.branches[branchIndex].state == kBranchState_Leaf)
        {
          byte leafFace = CW_FROM_FACE(plantState.rootFace, 2 * branchIndex);
          enqueueCommOnFace(leafFace, CommandType_BlossomFlower, 0, false);
        }
      }
    }

    // Send excess energy along grown branches
    // Always keep 3 energy to maintain our branches
    if (plantState.branches[1].state != kBranchState_Empty &&
        plantState.branches[2].state != kBranchState_Empty &&
        plantState.energyTotal > 3)
    {
      byte energyToSend = (plantState.energyTotal - 3) >> 1;
      energyToSend = MIN(energyToSend, 15); // clamp to max value we can send

      if (energyToSend > 0)
      {
        // Skip root branch (index 0)
        for (int branchIndex = 1; branchIndex < 3; branchIndex++)
        {
          // Branch is either 2 or 4 faces away from the root
          byte leafFace = CW_FROM_FACE(plantState.rootFace, 2 * branchIndex);
          enqueueCommOnFace(leafFace, CommandType_DistEnergy, energyToSend, true);
        }
      
        plantState.energyTotal -= energyToSend << 1;
      }
    }

    // Sunlight down to the root, but only if we have growth at the base
    if (plantState.branches[0].state != kBranchState_Empty)
    {
      // Add the sun gathered by our own leaves
      for (int branchIndex = 0; branchIndex < 3; branchIndex++)
      {
        if (plantState.branches[branchIndex].didGatherSun)
        {
          plantState.branches[branchIndex].didGatherSun = 0;
          gatheredSun++;
        }
      }

      // Send it on
      if (gatheredSun > 0)
      {
        enqueueCommOnFace(plantState.rootFace, CommandType_GatherSun, gatheredSun, true);
        gatheredSun = 0;
      }
    }
    
    plantEnergyTimer.set(PLANT_ENERGY_RATE);
  }
}

void resetFlowerTimer()
{
  plantFlowerTimer.set(PLANT_FLOWER_RATE);
}
#endif

// =================================================================================================
//
// SUN
//
// =================================================================================================

#if INCLUDE_SPECIAL_TILES
void loopSun()
{
  if (generateSunTimer.isExpired())
  {
    // Send out sunlight rays in all directions, three hexes wide
    FOREACH_FACE(f)
    {
      enqueueCommOnFace(f, CommandType_SendSun,     SUN_STRENGTH * sunStrengthScale, true);
      enqueueCommOnFace(f, CommandType_SendSunCW,   SUN_STRENGTH * sunStrengthScale, true);
      enqueueCommOnFace(f, CommandType_SendSunCCW,  SUN_STRENGTH * sunStrengthScale, true);
    }

    generateSunTimer.set(GENERATE_SUN_RATE);
  }
}
#endif

// =================================================================================================
//
// GRAVITY
//
// =================================================================================================

#if INCLUDE_SPECIAL_TILES
void loopGravity()
{
  if (sendGravityTimer.isExpired())
  {
    // Tell neighbors how many faces away is "up".
    // Since tiles can be oriented any arbitrary direction, this 
    // information must be relative to the face that is sending it.
    propagateGravityDir(99);

    sendGravityTimer.set(SEND_GRAVITY_RATE);
  }
}
#endif

// Outside the INCLUDE_SPECIAL_TILES because normal tiles need to propagate gravity too
void propagateGravityDir(byte exceptFace)
{
  // Start sending from our version of "up" to make things easier to iterate
  char ccwFromUp = 3;
  for (byte faceOffset = 0; faceOffset < FACE_COUNT; faceOffset++)
  {
    byte f = CCW_FROM_FACE(gravityUpFace, faceOffset);

    if (NeighborSynced(f))
    {
      // Want a way to avoid sending back to the face that told us
      if (f != exceptFace)
      {
        enqueueCommOnFace(f, CommandType_GravityDir, ccwFromUp, true);
      }
    }
    
    ccwFromUp--;
    if (ccwFromUp < 0)
    {
      ccwFromUp = 5;
    }
  }
}

// =================================================================================================
//
// BUG
//
// =================================================================================================

void loopBug()
{
  if (tileFlags & kTileFlag_HasBug)
  {
    // Did the bug flap its wings?
    if (bugFlapTimer.isExpired())
    {
      // Could just do X=1-X, but this is more readable
      bugFlapOpen = 1 - bugFlapOpen;
      bugFlapTimer.set(BUG_FLAP_RATE);

      // Move the bug along its path
      if (bugDirection == 0)
      {
        // Just sit there I guess?
      }
      else
      {
        bugDistance += (bugDirection > 0) ? BUG_MOVE_RATE : -BUG_MOVE_RATE;
        if (bugDistance > 64)
        {
          // Start moving back towards the center
          bugDistance = 64;
          bugDirection = -1;
          
          // While doing this, try to transfer to neighbor cell
          // If the transfer is accepted then the bug will leave this tile
          byte otherFace = CCW_FROM_FACE(bugTargetCorner, 1);
          byte tryTransferToFace = 0;
          CommandType commandType = CommandType_None;
          if (NeighborSynced(bugTargetCorner))
          {
            tryTransferToFace = bugTargetCorner;
            commandType = CommandType_TryTransferCW;
          }
          else if (NeighborSynced(otherFace))
          {
            // Choose the other face if the first face isn't present
            // If both faces are present, then do a coin flip
            // Using the LSB of the time *should* be random enough for a coin flip
            if (tryTransferToFace == 0 || currentTime & 0x1)
            {
              tryTransferToFace = otherFace;
              commandType = CommandType_TryTransfer;
            }
          }

          enqueueCommOnFace(tryTransferToFace, commandType, kTransferType_Bug, false);
        }
        else if (bugDistance < 0)
        {
          bugDistance = 0;
          bugDirection = 1;
          // Pick a different corner
          char offset = 3;
          switch (currentTime & 0x3)
          {
            case 0: offset = 3; break;
            case 1: offset = 2; break;
            case 2: offset = 3; break;
            case 3: offset = 4; break;
          }
          bugTargetCorner = CW_FROM_FACE(bugTargetCorner, offset);
        }
      }

    }
  }
}

// =================================================================================================
//
// RENDER
//
// =================================================================================================

#if NEW_RENDER
void render()
{
#if INCLUDE_BASE_TILES
  // Render algorithm
  //
  // Plants are in the background and fade in while growing/dying using the render timer
  // Bugs obscure plants (fly in front of them) and are immediate - they flap too fast to fade in/out
  // Water adjusts the rendered hue to be more blue
  // Sun adjusts the brightness according to how much sun the tile saw
  if (tileRole == kTileRole_Base)
  {
    // Clear the color
    FOREACH_FACE(f)
    {
      renderHueOnFace[f][kRenderBuffer_LerpNext] = 0;
    }
    
    // PLANT
    for (int branchIndex = 0; branchIndex < 3; branchIndex++)
    {
      byte leafFace = CW_FROM_FACE(plantState.rootFace, 2 * branchIndex);
      switch (plantState.branches[branchIndex].state)
      {
        case kBranchState_Branch:
          renderHueOnFace[leafFace][kRenderBuffer_LerpNext] = HUE_BRANCH;
          break;

        case kBranchState_Leaf:
          renderHueOnFace[leafFace][kRenderBuffer_LerpNext] = HUE_LEAF;
          break;
          
        case kBranchState_Flower:
          renderHueOnFace[leafFace][kRenderBuffer_LerpNext] = HUE_FLOWER;
          break;
      }
    }

    // BUG
    if (tileFlags &= kTileFlag_HasBug)
    {
      if (bugFlapOpen == 0)
      {
        renderHueOnFace[bugTargetCorner][kRenderBuffer_LerpNext] = HUE_BUG;
        renderHueOnFace[CW_FROM_FACE(bugTargetCorner, 5)][kRenderBuffer_LerpNext] = HUE_BUG;
      }
      else  // must be BUG_FLAP_OPENED
      {
        renderHueOnFace[CW_FROM_FACE(bugTargetCorner, 1)][kRenderBuffer_LerpNext] = HUE_BUG;
        renderHueOnFace[CW_FROM_FACE(bugTargetCorner, 4)][kRenderBuffer_LerpNext] = HUE_BUG;
      }
    }

    FOREACH_FACE(f)
    {
      // Factor in water
      if (faceStates[f].waterLevel > 0)
      {
        // If there is no other color already present, just make it blue
        if (renderHueOnFace[f][kRenderBuffer_LerpNext] == 0)
        {
          renderHueOnFace[f][kRenderBuffer_LerpNext] = HUE_WATER;
        }
        else
        {
          /*
          // Otherwise, skew the hue towards blue
          // Keep brightness as is
          renderHueOnFace[f][kRenderBuffer_LerpNext] = HUE_WATER;
          if (renderHueOnFace[f][kRenderBuffer_LerpNext] > (HUE_WATER - 128))
          {
            renderHueOnFace[f][kRenderBuffer_LerpNext] = (renderHueOnFace[f][kRenderBuffer_LerpNext] + HUE_WATER) >> 1;
          }
          else
          {
            renderHueOnFace[f][kRenderBuffer_LerpNext] = 255 - 42 - (renderHueOnFace[f][kRenderBuffer_LerpNext] >> 1);
          }
          */
        }
      }
    }

    // Render the actual color
    // Linearly interpolate between the current and next color to avoid jarring transitions
    FOREACH_FACE(f)
    {
      // Did time wrap around?
      byte t = currentTime & 0xFF;
      if (frameTime > t)
      {
        // Swap buffers!
        renderHueOnFace[f][kRenderBuffer_LerpFrom] = renderHueOnFace[f][kRenderBuffer_LerpTo];
        renderHueOnFace[f][kRenderBuffer_LerpTo] = renderHueOnFace[f][kRenderBuffer_LerpNext];
      }

      // Factor in sunlight, but only on faces that actually have something there
      byte bFrom = 128;
      if (renderHueOnFace[f][kRenderBuffer_LerpFrom] != 0 &&
          renderHueOnFace[f][kRenderBuffer_LerpFrom] != HUE_WATER)
      {
        bFrom += (sunSeen >= 0) ? 127 : sunSeen * 16;
      }
      byte bTo = 128;
      if (renderHueOnFace[f][kRenderBuffer_LerpTo] != 0 &&
          renderHueOnFace[f][kRenderBuffer_LerpTo] != HUE_WATER)
      {
        bTo += (sunSeen >= 0) ? 127 : sunSeen * 16;
      }

      // Lerp in RGB space since it's easier
      Color colorFrom = makeColorHSB(renderHueOnFace[f][kRenderBuffer_LerpFrom], 255, bFrom);
      Color colorTo   = makeColorHSB(renderHueOnFace[f][kRenderBuffer_LerpTo],   255, bTo  );

      byte r = (colorFrom.r * (0xFF - t) + colorTo.r * t) >> 8;
      byte g = (colorFrom.g * (0xFF - t) + colorTo.g * t) >> 8;
      byte b = (colorFrom.b * (0xFF - t) + colorTo.b * t) >> 8;

      setColorOnFace(makeColorRGB(r, g, b), f);
      //setColorOnFace(colorFrom, f);
    }

/*
    if (worstFrameTime & 0x80)
    {
      setColorOnFace(WHITE, 0);
    }
    if (worstFrameTime & 0x40)
    {
      setColorOnFace(WHITE, 1);
    }
    if (worstFrameTime & 0x20)
    {
      setColorOnFace(WHITE, 2);
    }
    if (worstFrameTime & 0x10)
    {
      setColorOnFace(WHITE, 3);
    }
    if (worstFrameTime & 0x8)
    {
      setColorOnFace(WHITE, 4);
    }
    if (worstFrameTime & 0x4)
    {
      setColorOnFace(WHITE, 5);
    }
*/
  }
#endif
}
#else
byte getWaveBrightness(byte state, byte value)
{
  switch (state)
  {
    case 0: return 0; break;
    case 1: return value; break;
    case 2: return 255; break;
    case 3: return 255 - value; break;
  }

  return 255;
}

char waveFacePairs[][2]
{
  {0, 0}, {1, 5}, {2, 4}, {3, 3}
};

void render()
{
  Color color;
  
  setColor(OFF);

  // Show the base tile state first
  switch(tileRole)
  {
    case kTileRole_Base:
      // already set tiles to black - don't need to do anything else
      //setColorOnFace(dim(WHITE, 64), 1);
      break;

#if INCLUDE_SPECIAL_TILES
    case kTileRole_Dripper:
      {
#if NEW_DRIPPER_RENDER
        unsigned long timeForWave = currentTime << dripperSpeedScale;
        unsigned long timeDiff = 512;

        for (int waveFacePairIndex = 0; waveFacePairIndex < 4; waveFacePairIndex++)
        {
          byte brightness = getWaveBrightness((timeForWave & 0x600) >> 9, timeForWave >> 1);
          color = makeColorHSB(HUE_DRIPPER, 255, brightness);
          setColorOnFace(color, waveFacePairs[waveFacePairIndex][0]);
          setColorOnFace(color, waveFacePairs[waveFacePairIndex][1]);
          timeForWave -= timeDiff;
        }

        /*
        brightness = getWaveBrightness((timeForWave & 0x600) >> 9, timeForWave >> 1);
        color = makeColorHSB(HUE_DRIPPER, 255, brightness);
        setColorOnFace(color, 1);
        setColorOnFace(color, 5);
        
        timeForWave -= timeDiff;
        brightness = getWaveBrightness((timeForWave & 0x600) >> 9, timeForWave >> 1);
        color = makeColorHSB(HUE_DRIPPER, 255, brightness);
        setColorOnFace(color, 2);
        setColorOnFace(color, 4);
        
        timeForWave -= timeDiff;
        brightness = getWaveBrightness((timeForWave & 0x600) >> 9, timeForWave >> 1);
        color = makeColorHSB(HUE_DRIPPER, 255, brightness);
        setColorOnFace(color, 3);
        */
        
/*
        // Make a wave animation flowing down the tile in the direction of gravity
        unsigned long timeForWave = currentTime << dripperSpeedScale;
        unsigned long timeDiff = 256;
        byte brightness = 0;
        switch (timeForWave & 0x600)
        {
          case 0x000: brightness = 0; break;
          case 0x200: brightness = (timeForWave & 0x1FF) >> 1; break;
          case 0x400: brightness = 255; break;
          case 0x600: brightness = 255 - ((timeForWave & 0x1FF) >> 1); break;
        }
        color = makeColorHSB(HUE_DRIPPER, 255, brightness);
        setColorOnFace(color, 5);
        setColorOnFace(color, 0);
        setColorOnFace(color, 1);

        timeForWave -= timeDiff;
        switch (timeForWave & 0x600)
        {
          case 0x000: brightness = 0; break;
          case 0x200: brightness = (timeForWave & 0x1FF) >> 1; break;
          case 0x400: brightness = 255; break;
          case 0x600: brightness = 255 - ((timeForWave & 0x1FF) >> 1); break;
        }
        color = makeColorHSB(HUE_DRIPPER, 255, brightness);
        setColorOnFace(color, 2);
        setColorOnFace(color, 3);
        setColorOnFace(color, 4);
*/
#else
        // Make a wave around the tile
        // Cycle every 2 seconds (2048 ms)
        // Brighter/faster as the dripper rate increases
        unsigned long faceTimeDiff = 341; // 2048 / 6 = 341-ish
        unsigned long timeForFace = currentTime << dripperSpeedScale;
        byte baseBrightness = 64 * dripperSpeedScale;
        FOREACH_FACE(f)
        {
          byte brightnessScale = ((timeForFace & 0x3FF) >> 2);
          if (timeForFace & 0x400)
          {
            brightnessScale = 255 - brightnessScale;
          }
  
          byte brightness = baseBrightness + (((255 - baseBrightness) * brightnessScale) >> 8);
          
          setColorOnFace(makeColorHSB(HUE_DRIPPER, 255, brightness), f);
          timeForFace += faceTimeDiff;
        }
#endif
      }
      break;

    case kTileRole_Dirt:
      {
        setColor(makeColorHSB(HUE_DIRT, 255, 128 + sunBrightness));
        /*
#if DEBUG_COLORS
        if (dirtReservoir >= MAX_DIRT_WATER)
        {
          setColorOnFace(COLOR_DIRT_FULL, 2);
          setColorOnFace(COLOR_DIRT_FULL, 3);
          setColorOnFace(COLOR_DIRT_FULL, 4);
        }
#endif
*/
      }
      break;

    case kTileRole_Sun:
      {
        // Make sun pulse
        // Cycle every 4 seconds (4096 ms)
        // Brighter/faster as the sun rate increases
        unsigned long timeForFace = currentTime << sunStrengthScale;
        FOREACH_FACE(f)
        {
          unsigned long brightness = ((timeForFace & 0x7FF) >> 3);
          if (timeForFace & 0x800)
          {
            brightness = 255 - brightness;
          }
            
          setColorOnFace(makeColorHSB(HUE_SUN, 255, brightness), f);
        }
      }
      break;
#endif
  }

  if (tileRole == kTileRole_Base)
  {
    FOREACH_FACE(f)
    {
#if DEBUG_COLORS
      // Sun in the very background
      // Would be nice to eventually have the sun light up the other elements
      {
        if (tileFlags & kTileFlag_PulseSun)
        {
          tileFlags &= ~kTileFlag_PulseSun;
          sunPulseTimer.set(SUN_PULSE_RATE);
        }
  
        byte sunDim = sunPulseTimer.getRemaining() >> 2;
        setColorOnFace(dim(makeColorHSB(HUE_SUN, 255, 255), sunDim), f);
      }
#endif

      // Water overrides sun
      if (faceStates[f].waterLevel > 0)
      {
//        byte dimness = faceStates[f].waterLevel << 4;    // water level 1 = brightness 16
//        Color waterColor = dim(COLOR_WATER, dimness);
        color = makeColorHSB(HUE_WATER, 255, 64);// + sunBrightness);
#if DEBUG_COLORS
        if (faceStates[f].waterLevel >= MAX_WATER_LEVEL)
        {
          // Special case for full water just so we can see it
          waterColor = COLOR_WATER_FULL;
          if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+1))
          {
            waterColor = COLOR_WATER_FULL1;
            if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+2))
            {
              waterColor = COLOR_WATER_FULL2;
              if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+3))
              {
                waterColor = COLOR_WATER_FULL3;
                if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+4))
                {
                  waterColor = COLOR_WATER_FULL4;
                  if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+5))
                  {
                    waterColor = COLOR_WATER_FULL5;
                    if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+6))
                    {
                      waterColor = COLOR_WATER_FULL6;
                      if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+7))
                      {
                        waterColor = COLOR_WATER_FULL7;
                        if (faceStates[f].waterLevel >= (MAX_WATER_LEVEL+8))
                        {
                          waterColor = COLOR_WATER_FULL8;
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
#endif
        setColorOnFace(color, f);
      }

      // Plant in the foreground
      for (int branchIndex = 0; branchIndex < 3; branchIndex++)
      {
        byte leafFace = CW_FROM_FACE(plantState.rootFace, 2 * branchIndex);
        switch (plantState.branches[branchIndex].state)
        {
#if DEBUG_COLORS
          case kBranchState_Empty:
          {
            if (plantState.branches[branchIndex].growthEnergy == 1)
            {
              setColorOnFace(COLOR_PLANT_GROWTH1, leafFace);
            }
            else if (plantState.branches[branchIndex].growthEnergy == 2)
            {
              setColorOnFace(COLOR_PLANT_GROWTH2, leafFace);
            }
            else if (plantState.branches[branchIndex].growthEnergy == 3)
            {
              setColorOnFace(COLOR_PLANT_GROWTH3, leafFace);
            }
          }
          break;
#endif

          case kBranchState_Branch:
            color = makeColorHSB(HUE_BRANCH, 255, 128 + sunBrightness);
            setColorOnFace(color, leafFace);
            break;

          case kBranchState_Leaf:
            color = makeColorHSB(HUE_LEAF, 255, 128 + sunBrightness);
            setColorOnFace(color, leafFace);
            break;
            
          case kBranchState_Flower:
            color = makeColorHSB(HUE_FLOWER, 255, 128 + sunBrightness);
            setColorOnFace(color, leafFace);
            break;
        }
      }

      // Bug in front of plant
      if (tileFlags &= kTileFlag_HasBug)
      {
        color = makeColorHSB(HUE_BUG, 255, 128 + sunBrightness);
        if (bugFlapOpen == 0)
        {
          setColorOnFace(color, bugTargetCorner);
          setColorOnFace(color, CW_FROM_FACE(bugTargetCorner, 5));
        }
        else  // must be BUG_FLAP_OPENED
        {
          setColorOnFace(color, CW_FROM_FACE(bugTargetCorner, 1));
          setColorOnFace(color, CW_FROM_FACE(bugTargetCorner, 4));
        }
      }
    }
  }

#if DEBUG_COLORS
  // Debug to show gravity
  if (gravityUpFace != GRAVITY_UP_FACE_UNKNOWN)
  {
    setColorOnFace(WHITE, gravityUpFace);
  }
#endif
  
  // Error codes
  FOREACH_FACE(f)
  {
    if (faceStates[f].flags & FaceFlag_Debug)
    {
      setColorOnFace(makeColorRGB(255,128,64), f);
    }
    
    if (ErrorOnFace(f))
    {
      setColorOnFace(makeColorRGB(255,0,0), f);
    }

/*
    switch (faceStates[f].faceColor)
    {
      case 1: setColorOnFace(COLOR_DEBUG1, f); break;
      case 2: setColorOnFace(COLOR_DEBUG2, f); break;
      case 3: setColorOnFace(COLOR_DEBUG3, f); break;
    }
*/
  }

#if DEBUG_COMMS
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];

    if (ErrorOnFace(f))
    {
      if (commInsertionIndexes[f] == COMM_INDEX_ERROR_OVERRUN)
      {
        setColorOnFace(MAGENTA, f);
      }
      else if (commInsertionIndexes[f] == COMM_INDEX_OUT_OF_SYNC)
      {
        setColorOnFace(ORANGE, f);
      }
      else if (commInsertionIndexes[f] == COMM_DATA_OVERRUN)
      {
        setColorOnFace(RED, f);
      }
      else
      {
        setColorOnFace(GREEN, f);
      }
    }
    else if (!isValueReceivedOnFaceExpired(f))
    {
      if (faceState->ourState == 2)
      {
        setColorOnFace(GREEN, f);
      }
      else if (faceState->ourState == 3)
      {
        setColorOnFace(BLUE, f);
      }
    }
  }
#endif

  // Reset how much sun we have seen since the last reset
  // Deal with this here because it affects how the tile renders
  if (sunSeenTimer.isExpired())
  {
    sunBrightness = (sunSeen > 0) ? 127 : (sunSeen << 4);
    /*
    byte newSunBrightness = (sunSeen >= 8) ? 127 : (sunSeen << 4);
    if (newSunBrightness > sunBrightness)
    {
      sunBrightness = newSunBrightness;
    }
    else if (sunBrightness > 0)
    {
      sunBrightness--;
    }
    */

    sunSeen = sunSeen >> 1;
    sunSeenTimer.set(SUN_SEEN_RATE);
  }
}
#endif  // NEW_RENDER
