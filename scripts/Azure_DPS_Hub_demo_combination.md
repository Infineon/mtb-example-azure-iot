To test the demo features of application's **option - 2:** **Azure Device App** on the newly provisioned device -

- **X509 Authentication Mode**
		
	1. In the _source/mqtt_main.h_ file, update the _device ID_ and _hub name/host name_ on which the device has been registered in the macro `MQTT_CLIENT_IDENTIFIER_AZURE_CERT` and `IOT_DEMO_SERVER_AZURE` respectively. The `device ID` and the `Hub name/Host Name` on which the device is registered is shown in _Figure 4: Provisioning status message_ in the operation section of _< application >/README.md_.

	2. Since the Azure Hub device is already created, follow the instructions after device creation step in "Without Azure DPS" flow in the corresponding sections from the Operation section of _README.md_ till board programming and the loading of menu on the terminal. Do not update the Azure RootCA, Device Certificate, Private keys in the _source/mqtt_main.h_ file (for non-secure kit) or provision (for secure kit). The same certificates and keys used during registration of device through DPS will be used for IoT hub operations.

	3. Now, from the menu the following Azure Hub features can be tested - _**2. Azure Device App (C2D, Telemetry, Methods, Twin)**_ and _**3. PnP (Plug and Play)**_.

- **SAS Authentication Mode**
	
	1. In the _source/mqtt_main.h_ file, update the _device ID_ and _hub name/host name_ on which the device has been registered in the macro `MQTT_CLIENT_IDENTIFIER_AZURE_SAS` and `IOT_DEMO_SERVER_AZURE` respectively. The `device ID` and the `Hub name/Host Name` on which the device is registered is shown in _Figure 4: Provisioning status message_ in the operation section of _< application >/README.md_.

	2. Since the Azure Hub device is already created, follow the instructions after device creation step in "Without Azure DPS" flow in the corresponding sections from the Operation section of _README.md_ till board programming and the loading of menu on the terminal. Generate the SAS token for the device registered on the Azure Hub and update it in _source/mqtt_main.h_ file (for non-secure kit) or provision (for secure kit).

	3. Now, from the menu the following Azure Hub features can be tested - _**2. Azure Device App (C2D, Telemetry, Methods, Twin)**_ and _**3. PnP (Plug and Play)**_.
