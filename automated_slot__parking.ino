void setup() {
void monitorParking() {
  unsigned long now = millis();
  int freeCount = 0;
  bool changeDetected = false;

  // 1. Scan PIR Sensors with Software Smoothing Filter
  for (int i = 0; i < 4; i++) {
    bool currentReading = (digitalRead(IR_PINS[i]) == HIGH); // HIGH = PIR Motion Detected
   
    if (currentReading) {
      // Motion detected! Instantly flag as occupied and update the active timestamp
      if (!slots[i].isOccupied) {
        slots[i].isOccupied = true;
        changeDetected = true;
      }
      slots[i].lastMotionAt = now; // Keep refreshing the window while there is movement
    } else {
      // No motion detected right now. Wait for a 60-second grace period before freeing up the slot
      if (slots[i].isOccupied && (now - slots[i].lastMotionAt > 60000UL)) {
        slots[i].isOccupied = false;
        changeDetected = true;
      }
    }

    // Handle Cloud Bookings Expiry
    if (slots[i].isBooked && (now - slots[i].bookedAt >= slots[i].durationMs)) {
      slots[i].isBooked = false;
      changeDetected = true;
    }
   
    // Handle Walk-In/Ultrasonic Expiry Hold
    if (slots[i].isAutoBooked && !slots[i].isOccupied && (now - slots[i].bookedAt > 30000UL)) {
      slots[i].isAutoBooked = false;
      changeDetected = true;
    }

    // A slot is completely free only if there are no app bookings, no gate holds, and no active PIR presence
    if (!slots[i].isBooked && !slots[i].isAutoBooked && !slots[i].isOccupied) {
      freeCount++;
    }
  }

  // 2. Scan Ultrasonic Sensor for Walk-ins at Gate
  long distance = getUltrasonicDistance();
  if (distance > 0 && distance < 15) {
    int assignedSlot = -1;
    for (int i = 0; i < 4; i++) {
      if (!slots[i].isBooked && !slots[i].isAutoBooked && !slots[i].isOccupied) {
        assignedSlot = i;
        break;
      }
    }

    if (assignedSlot != -1) {
      slots[assignedSlot].isAutoBooked = true;
      slots[assignedSlot].bookedAt = now;
      slots[assignedSlot].lastMotionAt = now; // Give the driver 60 seconds to physically pull into the slot
      freeCount--;
     
      lcd.clear();
      lcd.print("Go to Slot: "); lcd.print(assignedSlot + 1);
     
      gate.write(90);
      timer.setTimeout(4000L, [](){ gate.write(0); });
      changeDetected = true;
      delay(1500);
    }
  }

  // 3. Print Logs to Desktops if state changes
  if (changeDetected) {
    updateStatusReports(freeCount);
  }

  // Continuous Blynk App Cloud Sync
  if (Blynk.connected()) {
    Blynk.virtualWrite(V5, freeCount);
    int s = selectedSlot;
    if (slots[s].isBooked) {
      char buffer[20];
      sprintf(buffer, "Booked until %02d:%02d", slots[s].endHour, slots[s].endMinute);
      Blynk.virtualWrite(V2, buffer);
    } else {
      Blynk.virtualWrite(V2, slots[s].isOccupied ? "Occupied" : "Available");
    }
  }

  // Continuous Physical LCD Update
  lcd.setCursor(0, 0);
  lcd.print("Free Slots: ");
  lcd.print(freeCount);
  lcd.print("   ");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  for (int i = 0; i < 4; i++) {
    pinMode(IR_PINS[i], INPUT); // Standard Digital input configuration for PIR
    slots[i] = {false, false, false, 0, 0, 0, 0, 0};
  }

  gate.attach(SERVO_PIN, 500, 2400);
  gate.write(0);
 
  lcd.init();
  lcd.backlight();
  lcd.print("Booting Systems...");

  // Non-Blocking Network Verification
  WiFi.begin(ssid, pass);
  Serial.print("Connecting WiFi");
 
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 12) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Blynk.config(auth);
    Blynk.connect(1500);
  } else {
    Serial.println("\nWiFi Timeout! Hardware running in localized offline execution mode.");
  }

  lcd.clear();
  lcd.print("System Active");
  delay(1000);
  lcd.clear();
 
  timer.setInterval(1000L, monitorParking);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  timer.run();
}
