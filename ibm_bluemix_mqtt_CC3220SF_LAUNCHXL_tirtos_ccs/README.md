## Example Summary

This example introduce the user to the MQTT Client Library.


## Example Usage

* Connect to AP (Client Side)
	- AP information is set in network_if.h file.
	- In case hard coded AP SSID connection is failed, the user will be requested to fill the SSID of an open AP to connect to.
	- If no AP is available or connection failed, the example reset and try to connect again.

* After connection 
	- The application have one MQTT entity
		- MQTT Client - Can connect to remote broker (remote broker address is configurted in mqtt\_server.c (SERVER_ADDRESS)
	
  
* Client Topics
	- The Client is subscribe to topics 
			"/Broker/To/cc32xx"  
			"/cc3200/ToggleLEDCmdL1"  
			"/cc3200/ToggleLEDCmdL2"  
			"/cc3200/ToggleLEDCmdL3"  
	- The Client publish the following topic "/cc32xx/ButtonPressEvtSw2" - 
			the topic will be published by pressing SW2 on the board
			
* Secured socket 
	In order to activate the secured example, SECURE_CLIENET should be enabled  ( certificates should be programmed )
			
* Special handling
	- In case the internal client will disconnect (for any reason) from the remote broker, the MQTT will be restarted, The user can change that behavior.

		
## Application Design Details

* This example provides users the ability to work with Client MQTT
	- Subscribed topics
		The Client will connect to remote broker Subscribed to the topics
			"/Broker/To/cc32xx"  - General topic that will print the data on the uart  
			"/cc3200/ToggleLEDCmdL1" - toggle LED0  
			"/cc3200/ToggleLEDCmdL2" - toggle LED1  
			"/cc3200/ToggleLEDCmdL3" - toggle LED2  
			
	- Published topics
		the topic will be published by pressing SW2 on the board
			
## References

