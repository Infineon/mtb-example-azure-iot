To test the demo features of application's **option - 2:** **Azure Device App** on the newly provisioned device -

- **SAS Authentication Mode**
	
	1. In the _source/mqtt_main.h_ file, update the Hub on which the device is registered.
		
	2. Since the Azure Hub device is already created, follow the instructions after device creation step in "Without Azure DPS" flow in the corresponding sections from the Operation section of _README.md_, generate the SAS token for the device registered on the Azure Hub and update it in _mqtt_main.h_ file (for non-secure kit) or provision into the secure kit.
	
- **X509 Authentication Mode**
		
	1. In the _source/mqtt_main.h_ file, update the Hub on which the device is registered.
		
	2. Since the Azure Hub device is already created, follow the instructions after device creation step in "Without Azure DPS" flow in the corresponding sections from the Operation section of _README.md_, do not update the Azure RootCA, Device Certificate, Private keys in the _mqtt_main.h_ file (for non-secure kit) or provision into the secure kit. The same certificates and keys used during registration of device through DPS will be used for IoT hub operations.