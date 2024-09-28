To test the demo features of this application's **option - 2:** **Azure Device App** on the newly provisioned device -

- **X509 Authentication Mode**
		
	1. In the _source/mqtt_main.h_ file, update the _device ID_ and _hub name/host name_ on which the device has been registered in the `MQTT_CLIENT_IDENTIFIER_AZURE_CERT` and `IOT_DEMO_SERVER_AZURE` macros respectively. The `device ID` and the `Hub name/Host Name` on which the device is registered is shown in _Figure 4. Provisioning status message_ in the **Operation** section of the _< application >/README.md_.

	2. Since the Azure Hub device is already created, follow the instructions after the device creation step in the "Without Azure DPS" flow in the corresponding sections from the Authentication credentials setup section of _README.md_ until board programming and the loading of the menu on the terminal. Do not update the Azure RootCA, Device Certificate, and Private keys in the _source/mqtt_main.h_ file (for non-secure kit) or provision (for secure kit). The same certificates and keys used during registration of the device through DPS will be used for IoT hub operations.

	3. After that you can test the following Azure Hub features from the menu - _**2. Azure Device App (C2D, Telemetry, Methods, Twin)**_ and _**3. PnP (Plug and Play)**_.

- **SAS Authentication Mode**
	
	1. In the _source/mqtt_main.h_ file, update the _device ID_ and _hub name/host name_ on which the device has been registered in the `MQTT_CLIENT_IDENTIFIER_AZURE_SAS` and `IOT_DEMO_SERVER_AZURE` macros respectively. The `device ID` and the `Hub name/Host Name` on which the device is registered is shown in _Figure 4. Provisioning status message_ in the **Operation** section of the _< application >/README.md_.

	2. Since the Azure Hub device is already created, follow the instructions after the device creation step in the "Without Azure DPS" flow in the corresponding sections from the Authentication credentials setup section of _README.md_ until board programming and the loading of the menu on the terminal. Generate the SAS token for the device registered on the Azure Hub and update it in the _source/mqtt_main.h_ file (for non-secure kit) or provision (for secure kit).

	3. After that you can test the following Azure Hub features from the menu - _**2. Azure Device App (C2D, Telemetry, Methods, Twin)**_ and _**3. PnP (Plug and Play)**_.
