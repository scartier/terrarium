// DID
// * Sun?
// * Refactored water flow to save code space (~330 bytes)
// * Found max data (942 bytes) - Have > 140 bytes left
// * Bitwise structs eat into code space! Found by creating a memclr instead of setting all plant values to 0. Saved 300 bytes.
// * Got a comm overrun. Changed how plants gather sunlight to not overuse the comms.

// TODO
// * Change sense of neighbor water level to be how much it can accept
// * Toggle debug info with triple click

// WHEN HAVE MORE TILES
// * Ensure sunlight is absorbed/blocked by things correctly

#define null 0
//#define DEBUG_COMMS 1
//#define DEBUG_COLORS 1
#define NEW_WATER_CODE 1
#define INCLUDE_BASE_TILES    1
#define INCLUDE_SPECIAL_TILES 1

#define COLOR_DRIPPER     makeColorRGB(  0, 255, 128)
#define COLOR_DIRT        makeColorRGB(107,  80,   0)
#if DEBUG_COLORS
#define COLOR_DIRT_FULL   makeColorRGB(107,  80,  32)
#endif
#define COLOR_SUN         makeColorRGB(255, 255,   0)
#define COLOR_BUG         makeColorRGB(107,  80,   0)
#define COLOR_WATER       makeColorRGB(  0,   0, 128)

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

#define COLOR_LEAF         makeColorRGB(  0, 255,   0)
#define COLOR_BRANCH       makeColorRGB(152, 102,   0)

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
#define CCW_FROM_FACE(f, amt) (((f) - (amt)) + (((f) >= (amt)) ? 0 : 6))

// Gravity is a constant velocity for now (not accelerative)
// Might change that later if there's need
#define GRAVITY 300 // ms to fall from pixel to pixel
Timer gravityTimer;

enum eTileFlags
{
  kTileFlag_PulseSun = 1<<0,
};
byte tileFlags;

unsigned long currentTime = 0;
byte frameTime;

#define USE_DATA_SPONGE 0
#if USE_DATA_SPONGE
byte sponge[143];
#endif

// =================================================================================================
//
// SYSTEMS
//
// =================================================================================================

// States the player can select for a tile
// They cycle through by clicking the tile's button
enum eTileState
{
  kTileState_Empty,
  kTileState_Dripper,
  kTileState_Dirt,
  kTileState_Sun,

  kTileState_MAX
};

// Current tile state - starts empty
char tileState = kTileState_Empty;

// -------------------------------------------------------------------------------------------------
// DRIPPER/WATER
//
#define MAX_WATER_LEVEL 15  // water level can actually go above this, but will stop accepting new water

#if INCLUDE_SPECIAL_TILES
#define DRIPPER_AMOUNT 4
#define DRIPPER_RATE 1000   // ms between drips
Timer dripperTimer;
#endif

#define DIRT_EVAPORATION 4
#define EVAPORATION_RATE 5000
Timer evaporationTimer;

// -------------------------------------------------------------------------------------------------
// DIRT/PLANT
//
#define MAX_DIRT_RESERVOIR 100
#define MAX_GATHERED_SUN 200

#if INCLUDE_SPECIAL_TILES
byte dirtReservoir = 0;
#endif

#if INCLUDE_BASE_TILES
byte gatheredSun = 0;   // used by plants (dirt tiles use the per-face 'gatheredSun')
#endif

// Amount of water that seeps out of a saturated dirt tile
#define DIRT_WATER_SEEP 4

// Controls the rate at which the dirt distributes energy to its plants
// Also used by leaves/branches to continue distributing energy up the plant
#define ENERGY_DIST_RATE 5000
Timer energyDistTimer;

#define MAX_ENERGY_PER_TILE 24
enum eBranchState
{
  kBranchState_Empty,
  kBranchState_Leaf,
  kBranchState_Branch,
  kBranchState_Flower   // ??
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
// Controls the rate at which plants use energy to stay alive and grow
#define ENERGY_USE_RATE 5000
Timer energyUseTimer;
#endif

// -------------------------------------------------------------------------------------------------
// SUN
//
#if INCLUDE_SPECIAL_TILES
// Controls the rate at which sun tiles generate sunlight
#define SUN_STRENGTH 3
#define GENERATE_SUN_RATE 500
Timer generateSunTimer;
#endif

//#if DEBUG_COLORS
#define SUN_PULSE_RATE 250
Timer sunPulseTimer;
//#endif

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

enum NeighborSyncFlag
{
  NeighborSyncFlag_Present    = 1<<0,
  NeighborSyncFlag_GotWater   = 1<<1,
  NeighborSyncFlag_SentWater  = 1<<2,

  NeighborSyncFlag_Debug      = 1<<7
};

struct FaceState
{
  FaceValue faceValueIn;
  FaceValue faceValueOut;
  byte lastCommandIn;

  byte neighborSyncFlags;
  
  byte waterLevel;
  byte waterLevelNew;
  byte waterLevelNeighbor;
  byte waterLevelNeighborNew;
  
#if INCLUDE_SPECIAL_TILES
  byte gatheredSun;           // so dirt tiles send energy to the right face
#endif

  byte faceColor; // DEBUG

#if DEBUG_COMMS
  byte ourState;
  byte neighborState;
#endif
};
FaceState faceStates[FACE_COUNT];

#define NeighborSynced(f) ((faceStates[f].neighborSyncFlags & \
                              (NeighborSyncFlag_Present | NeighborSyncFlag_GotWater)) == \
                              (NeighborSyncFlag_Present | NeighborSyncFlag_GotWater))

enum CommandType
{
  CommandType_None,         // no data
  CommandType_WaterLevel,   // Tell Our Water Level : value=water level on this face
  CommandType_WaterAdd,     // Add Water To Neighbor : value=water level to add to the neighbor face
  CommandType_DistEnergy,   // Distribute Energy : value=energy
  CommandType_SendSun,      // Send sunlight (sent from Sun tiles): value=sun amount
  CommandType_SendSunCW,    // Send sunlight (sent from Sun tiles): value=sun amount (turns CW after one tile)
  CommandType_SendSunCCW,   // Send sunlight (sent from Sun tiles): value=sun amount (turns CCW after one tile)
  CommandType_GatherSun,    // Gather sun from leaves to root : value=sun amount
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

#define COMM_QUEUE_SIZE 8
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
  faceState->neighborSyncFlags &= ~NeighborSyncFlag_SentWater;
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
      faceStates[f].neighborSyncFlags = 0;
      continue;
    }

    // If there is any kind of error on the face then do nothing
    // The error can be reset by removing the neighbor
    if (ErrorOnFace(f))
    {
      continue;
    }

    FaceState *faceState = &faceStates[f];

    faceState->neighborSyncFlags |= NeighborSyncFlag_Present;

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
        // Use the saved command value to determine what to do with the data
        switch (faceState->lastCommandIn)
        {
          case CommandType_WaterLevel:
            faceState->waterLevelNeighbor = value;
            faceState->neighborSyncFlags |= NeighborSyncFlag_GotWater;
            break;
            
          case CommandType_WaterAdd:
            switch (tileState)
            {
              case kTileState_Empty:
                // Normal tiles accumulate water in each face
                faceState->waterLevel += value;
                replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceState->waterLevel, true);
                break;

#if INCLUDE_SPECIAL_TILES
              case kTileState_Dirt:
                // Dirt tiles have one large reservoir absorbed in the tile
                dirtReservoir += value;
                replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, getDirtWaterLevelToSend(), true);
                break;
#endif
            }
            break;
            
          case CommandType_DistEnergy:
            if (tileState == kTileState_Empty)
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
            break;

          case CommandType_SendSun:
          case CommandType_SendSunCW:
          case CommandType_SendSunCCW:
            if (tileState == kTileState_Empty)
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
#if INCLUDE_SPECIAL_TILES
            else if (tileState == kTileState_Dirt)
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
              byte exitFace = CCW_FROM_FACE(f, faceOffset);
              enqueueCommOnFace(exitFace, CommandType_SendSun, value, true);
            }
            break;
            
          case CommandType_GatherSun:
            if (tileState == kTileState_Empty)
            {
              // Plants propagate the sun until it gets to the root in the dirt
              if (plantState.branches[0].state != kBranchState_Empty)
              {
                if (gatheredSun < MAX_GATHERED_SUN)
                {
                  gatheredSun += value;
                }

/*
                // When receiving sun, convert our leaves to branches
                char faceOffsetFromRoot = plantState.rootFace - f;
                if (faceOffsetFromRoot == 2 || faceOffsetFromRoot == -4)
                {
                  plantState.branches[1].state = kBranchState_Branch;
                }
                else if (faceOffsetFromRoot == 4 || faceOffsetFromRoot == -2)
                {
                  plantState.branches[2].state = kBranchState_Branch;
                }
*/
              }
            }
#if INCLUDE_SPECIAL_TILES
            else if (tileState == kTileState_Dirt)
            {
              // Dirt tiles convert sun to energy!
              if (faceState->gatheredSun < MAX_GATHERED_SUN)
              {
                faceState->gatheredSun += value;
              }
            }
#endif
            break;
          
#if DEBUG_COMMS
          case CommandType_UpdateState:
            faceState->ourState = value;
            break;
#endif
        }
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
  // Clamp frame time at 250 ms to fit within a byte
  // Hopefully processing doesn't ever take longer than that for one frame
  /*
  unsigned long previousTime = currentTime;
  currentTime = millis();
  unsigned long timeSinceLastLoop = currentTime - previousTime;
  frameTime = (timeSinceLastLoop > 250) ? 250 : 0;
  */
  
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
#else

  // Systems updates
  switch (tileState)
  {
#if INCLUDE_BASE_TILES
    case kTileState_Empty:
      loopWater();
      loopPlant();
      break;
#endif
      
#if INCLUDE_SPECIAL_TILES
    case kTileState_Dripper:  loopDripper();  break;
    case kTileState_Dirt:     loopDirt();     break;
    case kTileState_Sun:      loopSun();      break;
#endif
  }  

  // Update water levels and such
  postProcessState();
#endif

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
  if (buttonDoubleClicked())
  {
    // Reset our state and tell our neighbors to reset their perception of us
    resetOurState();
  }

#if INCLUDE_SPECIAL_TILES
  if (buttonSingleClicked())
  {
    // User state changed

    char prevTileState = tileState;
    
    tileState++;
    if (tileState >= kTileState_MAX)
    {
      tileState = 0;
    }

    // Reset our state and tell our neighbors to reset their perception of us
    resetOurState();

    if (prevTileState != tileState)
    {
      switch (prevTileState)
      {
        case kTileState_Dripper:
          break;
      }

      switch (tileState)
      {
        case kTileState_Dripper:
          dripperTimer.set(DRIPPER_RATE);   // start dripping
          break;

        case kTileState_Dirt:
          energyDistTimer.set(ENERGY_DIST_RATE);
          break;
      }
    }
  }  // buttonSingleClicked
#endif
}

void resetOurState()
{
  FOREACH_FACE(f)
  {
    FaceState *faceState = &faceStates[f];

    // Clear sync flags dealing with our state
    faceState->neighborSyncFlags &= ~NeighborSyncFlag_SentWater;

    switch (tileState)
    {
      case kTileState_Empty:
        faceState->waterLevel = 0;
        faceState->waterLevelNew = 0;

#if 1
        byte *ptr = (byte*) &plantState;
        byte *ptrEnd = (byte*) &plantState + sizeof(plantState);
        while (ptr != ptrEnd)
        {
          *ptr = 0;
          ptr++;
        }
#else
        plantState.branches[0].state = kBranchState_Empty;
        plantState.branches[1].state = kBranchState_Empty;
        plantState.branches[2].state = kBranchState_Empty;
        plantState.branches[0].growthEnergy = 0;
        plantState.branches[1].growthEnergy = 0;
        plantState.branches[2].growthEnergy = 0;
        plantState.energyTotal = 0;
#endif
        break;

#if INCLUDE_SPECIAL_TILES
      case kTileState_Dripper:
        dripperTimer.set(1);
        break;
        
      case kTileState_Dirt:
        faceState->waterLevel = MAX_WATER_LEVEL;
        faceState->gatheredSun = 0;
        dirtReservoir = 0;
        break;

      case kTileState_Sun:
        generateSunTimer.set(1);
        break;
#endif
    }

#if INCLUDE_BASE_TILES
    gatheredSun = 0;
#endif
  }

  // Remove all plant energy - causes all plants to die immediately
  plantState.energyTotal = 0;

  tileFlags = 0;
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
    if (faceState->neighborSyncFlags & NeighborSyncFlag_Present)
    {
      if (!(faceState->neighborSyncFlags & NeighborSyncFlag_SentWater))
      {
        byte waterLevel = faceState->waterLevel;
        switch (tileState)
        {
          case kTileState_Empty:
            // Empty tile uses actual water level
            break;

#if INCLUDE_SPECIAL_TILES
          case kTileState_Dripper:
            // Drippers cannot accept water - it just falls around them
            waterLevel = MAX_WATER_LEVEL;
            break;

          case kTileState_Sun:
            // Suns destroy water that falls in - it is always empty
            waterLevel = 0;
            break;
            
          case kTileState_Dirt:
            // Dirt absorbs water over the entire tile
            waterLevel = getDirtWaterLevelToSend();
            break;
#endif
        }
        replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, waterLevel, true);
        faceState->neighborSyncFlags |= NeighborSyncFlag_SentWater;
      }
    }
  }

  // Water evaporation/seepage
  if (evaporationTimer.isExpired())
  {
    switch (tileState)
    {
      case kTileState_Empty:
        FOREACH_FACE(f)
        {
          if (faceStates[f].waterLevel > 0)
          {
            faceStates[f].waterLevel--;
            replaceOrEnqueueCommOnFace(f, CommandType_WaterLevel, faceStates[f].waterLevel, true);
          }
        }
        break;

#if INCLUDE_SPECIAL_TILES
      case kTileState_Dirt:
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

  // Used by both dirt tiles and plants, so must go here
  if (energyDistTimer.isExpired())
  {
    energyDistTimer.set(ENERGY_DIST_RATE);
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
    dripperTimer.set(DRIPPER_RATE);

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
#if NEW_WATER_CODE
    for (int waterFlowIndex = 0; waterFlowIndex < 16; waterFlowIndex++)
    {
      WaterFlowCommand command = waterFlowSequence[waterFlowIndex];

      FaceState *faceStateSrc = &faceStates[command.srcFace];
      FaceState *faceStateDst = &faceStates[command.dstFace];
      
      byte *dst = command.isNeighbor ? &faceStateDst->waterLevelNeighbor : &faceStateDst->waterLevel;
      byte *dstNew = command.isNeighbor ? &faceStateDst->waterLevelNeighborNew : &faceStateDst->waterLevelNew;
      byte amountToSend = faceStateSrc->waterLevel >> command.halfWater;
      amountToSend = MIN(amountToSend, MAX_WATER_LEVEL);
      
      if (!command.isNeighbor || NeighborSynced(command.dstFace))
      {
        if (tryFlowWater(&faceStateSrc->waterLevel, dst, dstNew, amountToSend) > 0)
        {
          replaceOrEnqueueCommOnFace(command.srcFace, CommandType_WaterLevel, faceStateSrc->waterLevel, true);
        }
      }
    }

#else // NEW_WATER_CODE

    // Water falls straight down within a tile, if possible.
    // If the neighbor below is full/blocked then flow to the sides.

    // FALL DOWN
    // Bottom row first (to make room for the top row)
    if (NeighborSynced(3))
    {
      if (tryFlowWater(&faceStates[3].waterLevel, &faceStates[3].waterLevelNeighbor, &faceStates[3].waterLevelNeighborNew, MAX_WATER_LEVEL) > 0)
      {
        replaceOrEnqueueCommOnFace(3, CommandType_WaterLevel, faceStates[3].waterLevel, true);
      }
    }
    // Top row last
    if (tryFlowWater(&faceStates[0].waterLevel, &faceStates[3].waterLevel, &faceStates[3].waterLevelNew, MAX_WATER_LEVEL) > 0)
    {
      replaceOrEnqueueCommOnFace(0, CommandType_WaterLevel, faceStates[0].waterLevel, true);
    }
    if (tryFlowWater(&faceStates[1].waterLevel, &faceStates[2].waterLevel, &faceStates[2].waterLevelNew, MAX_WATER_LEVEL) > 0)
    {
      replaceOrEnqueueCommOnFace(1, CommandType_WaterLevel, faceStates[1].waterLevel, true);
    }
    if (tryFlowWater(&faceStates[5].waterLevel, &faceStates[4].waterLevel, &faceStates[4].waterLevelNew, MAX_WATER_LEVEL) > 0)
    {
      replaceOrEnqueueCommOnFace(5, CommandType_WaterLevel, faceStates[5].waterLevel, true);
    }

    // FLOW TO SIDES
    waterFlowToSides(5,
                     &faceStates[0].waterLevel,
                     &faceStates[0].waterLevelNew,
                     NeighborSynced(5) ? &faceStates[5].waterLevelNeighbor : null,
                     &faceStates[5].waterLevelNeighborNew);
    waterFlowToSides(4,
                     &faceStates[3].waterLevel,
                     &faceStates[3].waterLevelNew,
                     NeighborSynced(4) ? &faceStates[4].waterLevelNeighbor : null,
                     &faceStates[4].waterLevelNeighborNew);
    waterFlowToSides(0,
                     &faceStates[5].waterLevel,
                     &faceStates[5].waterLevelNew,
                     &faceStates[1].waterLevel,
                     &faceStates[1].waterLevelNew);
    waterFlowToSides(3,
                     &faceStates[4].waterLevel,
                     &faceStates[4].waterLevelNew,
                     &faceStates[2].waterLevel,
                     &faceStates[2].waterLevelNew);
    waterFlowToSides(1,
                     &faceStates[0].waterLevel,
                     &faceStates[0].waterLevelNew,
                     NeighborSynced(1) ? &faceStates[1].waterLevelNeighbor : null,
                     &faceStates[1].waterLevelNeighborNew);
    waterFlowToSides(2,
                     &faceStates[3].waterLevel,
                     &faceStates[3].waterLevelNew,
                     NeighborSynced(2) ? &faceStates[2].waterLevelNeighbor : null,
                     &faceStates[2].waterLevelNeighborNew);

#endif    // NEW_WATER_CODE

  }
}

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

#if NEW_WATER_CODE == 0
void waterFlowToSides(char srcPixel, byte *dst1, byte *dst1New, byte *dst2, byte *dst2New)
{
  if (tryFlowWater(&faceStates[srcPixel].waterLevel, dst1, dst1New, faceStates[srcPixel].waterLevel >> 1) > 0)
  {
    replaceOrEnqueueCommOnFace(srcPixel, CommandType_WaterLevel, faceStates[srcPixel].waterLevel, true);
  }
  if (dst2 != null)
  {
    if (tryFlowWater(&faceStates[srcPixel].waterLevel, dst2, dst2New, faceStates[srcPixel].waterLevel >> 1) > 0)
    {
      replaceOrEnqueueCommOnFace(srcPixel, CommandType_WaterLevel, faceStates[srcPixel].waterLevel, true);
    }
  }
}
#endif

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
  if (energyDistTimer.isExpired())
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

void loopPlant()
{
  // Plants use energy periodically to stay alive and grow
  if (energyUseTimer.isExpired())
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
          plantState.branches[branchIndex].state = kBranchState_Leaf;

/*
          // Leaves on the outer branches convert the root to a branch
          if (branchIndex > 0)
          {
            plantState.branches[0].state = kBranchState_Branch;
          }
*/
        }

        // If the root branch is still growing, don't process the other branches
        if (branchIndex == 0)
        {
          break;
        }
      }
    }

    energyUseTimer.set(ENERGY_USE_RATE);
  }

  // Distribute excess energy along grown branches
  if (energyDistTimer.isExpired())
  {
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
          byte leafFace = CCW_FROM_FACE(plantState.rootFace, 2 * branchIndex);
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
  }

}

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
      enqueueCommOnFace(f, CommandType_SendSun,     SUN_STRENGTH, true);
      enqueueCommOnFace(f, CommandType_SendSunCW,   SUN_STRENGTH, true);
      enqueueCommOnFace(f, CommandType_SendSunCCW,  SUN_STRENGTH, true);
    }

    generateSunTimer.set(GENERATE_SUN_RATE);
  }
}
#endif

// =================================================================================================
//
// RENDER
//
// =================================================================================================

void render()
{
  setColor(OFF);

  // Show the base tile state first
  switch(tileState)
  {
    case kTileState_Empty:
      // already set tiles to black - don't need to do anything else
      //setColorOnFace(dim(WHITE, 64), 1);
      break;

#if INCLUDE_SPECIAL_TILES
    case kTileState_Dripper:
      // Intensity rises as it gets closer to dripping
      {
        long dimnessL = (dripperTimer.getRemaining() << 8) / DRIPPER_RATE;
        byte dimness = 255;
        if (dimnessL < 256)
        {
          dimness = 255 - dimnessL;
        }
        dimness = dimness >> 1;
        setColor(dim(COLOR_DRIPPER, dimness));
      }
      break;

    case kTileState_Dirt:
      {
        setColor(COLOR_DIRT);
#if DEBUG_COLORS
        if (dirtReservoir >= MAX_DIRT_WATER)
        {
          setColorOnFace(COLOR_DIRT_FULL, 2);
          setColorOnFace(COLOR_DIRT_FULL, 3);
          setColorOnFace(COLOR_DIRT_FULL, 4);
        }
#endif
      }
      break;

    case kTileState_Sun:
      {
        byte dimness = currentTime >> 1; // pulse every 512 ms
        if (currentTime & 0x200)
        {
          dimness = 255 - dimness;  // pulse up and down alternating
        }
        if (dimness < 192)
        {
          dimness = 192;
        }
        //dimness |= 0x80;  // force a minimum of brightness
        setColor(dim(COLOR_SUN, dimness));
      }
      break;
#endif
  }

  if (tileState == kTileState_Empty)
  {
    FOREACH_FACE(f)
    {
//#if DEBUG_COLORS
      // Sun in the very background
      // Would be nice to eventually have the sun light up the other elements
      {
        if (tileFlags & kTileFlag_PulseSun)
        {
          tileFlags &= ~kTileFlag_PulseSun;
          sunPulseTimer.set(SUN_PULSE_RATE);
        }
  
        byte sunDim = sunPulseTimer.getRemaining() >> 2;
        setColorOnFace(dim(COLOR_SUN, sunDim), f);
      }
//#endif

      // Water overrides sun
      if (faceStates[f].waterLevel > 0)
      {
        byte dimness = faceStates[f].waterLevel << 4;    // water level 1 = brightness 16
        Color waterColor = dim(COLOR_WATER, dimness);
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
        setColorOnFace(waterColor, f);
      }

      // Plant in the foreground
      for (int branchIndex = 0; branchIndex < 3; branchIndex++)
      {
        byte leafFace = CCW_FROM_FACE(plantState.rootFace, 2 * branchIndex);
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

/*
          case kBranchState_Branch:
            setColorOnFace(COLOR_BRANCH, leafFace);
            break;
*/

          case kBranchState_Leaf:
          {
            byte dimness = 128;
            if (plantState.branches[branchIndex].didGatherSun)
            {
              //dimness = 255;
            }
            setColorOnFace(dim(COLOR_LEAF, dimness), leafFace);
          }
          break;
        }
      }
    }
  }

  // Error codes
  FOREACH_FACE(f)
  {
    if (faceStates[f].neighborSyncFlags & NeighborSyncFlag_Debug)
    {
      setColorOnFace(makeColorRGB(255,128,64), f);
    }
    
    if (ErrorOnFace(f))
    {
      setColorOnFace(makeColorRGB(255,0,0), f);
    }

    switch (faceStates[f].faceColor)
    {
      case 1: setColorOnFace(COLOR_DEBUG1, f); break;
      case 2: setColorOnFace(COLOR_DEBUG2, f); break;
      case 3: setColorOnFace(COLOR_DEBUG3, f); break;
    }
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

}
