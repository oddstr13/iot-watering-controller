STRINGIFY(
<!DOCTYPE html>
<html>

<head>
	<meta charset="utf-8" />
	<title>Node configuration</title>
	<style>
		label {
			display: block;
		}

	</style>
</head>

<body>
	<h1>UKHASnet Gateway node configuration</h1>
	<form id="config">
		<label for="node_id">Node ID</label>
		<input name="node_id" type="text" maxlen="16" />
		<fieldset>
			<legend>WiFi</legend>
			<label for="ssid">SSID</label>
			<input name="ssid" type="text" maxlen="31" />

			<label for="password">Password</label>
			<input name="password" type="password" minlen="8" maxlen="63" />

			<label for="clear_password">Clear password?
				<input name="clear_password" type="checkbox" />
			</label>
		</fieldset>

		<fieldset>
			<legend>UKHASnet Upload API</legend>

			<label for="api_enabled">Enabled
				<input name="api_enabled" type="checkbox" />
			</label>

			<label for="api_url">URL</label>
			<input name="api_url" type="url" maxlen="255" />
		</fieldset>

		<fieldset>
			<legend>Radio</legend>

			<label for="tx_enabled" title="Act as repeater">Transmit
				<input name="tx_enabled" type="checkbox" />
			</label>

			<label for="packet_interval" title="How often to send packets (seconds)">Packet interval</label>
			<input name="packet_interval" type="number" min="1" max="604800" />
		</fieldset>

		<fieldset>
			<legend>Multicast</legend>

			<label for="multicast_enabled" title="Send packets over IPv6 multicast?">Enabled
				<input name="multicast_enabled" type="checkbox" />
			</label>

			<label for="multicast_address" title="UKHASnet packets go here">Address</label>
			<input name="multicast_address" type="text" maxlen="40" />

			<label for="multicast_address_other" title="Packets not matching the basic UKHASnet structure goes here">Address
				(other)</label>
			<input name="multicast_address_other" type="text" maxlen="40" />

			<label for="multicast_port">Port</label>
			<input name="multicast_port" type="number" min="1025" max="65535" />

			<label for="multicast_ttl">TTL</label>
			<input name="multicast_ttl" type="number" min="1" max="255" />
		</fieldset>

		<button type="submit">Set</button>
	</form>
	<script>
		let config_url = '/config.json';
		let config_data;

		function saveConfig() {
			let res = {};
			let form = document.forms.config;

			for (const el of form.elements) {
				let key = el.name;
				let value;
				if (key) {
					if (el.type == 'checkbox') {
						value = el.checked;
					} else if (el.value) {
						if (el.type == 'number') {
							value = el.valueAsNumber;
						} else {
							value = el.value;
						}
					}
				}
				if (value != config_data[key]) {
					res[key] = value;
				}
			}
			if ('packet_interval' in res) {
				res.packet_interval *= 1000;
			}
			console.log(res);


			let xhr = new XMLHttpRequest();
			xhr.open('PATCH', config_url, true);
			xhr.responseType = 'json';

			xhr.onload = function (e) {
				console.log(e);
				loadConfig();
			};

			xhr.send(JSON.stringify(res));
		}

		function _loadConfigReal(conf) {
			config_data = conf;
			config_data.clear_password = false;
			config_data.packet_interval /= 1000;
			console.log(config_data);

			let form = document.forms.config;
			form.elements.password.value = '';
			if (config_data.password_set) {
				form.elements.password.placeholder = '*****';
			} else {
				form.elements.password.placeholder = 'Empty'
			}

			for (const key in config_data) {
				if (key in form.elements) {
					let el = form.elements[key];

					el.placeholder = config_data[key];
					el.value = config_data[key];
					if (el.type == 'checkbox') {
						el.checked = config_data[key];
					}
				}
			}
		}

		function loadConfig() {
			let xhr = new XMLHttpRequest();
			xhr.open('GET', config_url, true);
			xhr.responseType = 'json';

			xhr.onload = function (e) {
				if (this.status == 200) {
					_loadConfigReal(this.response);
				}
			};

			xhr.send();
		}

		document.forms.config.addEventListener('submit', function (e) {
			e.preventDefault();
			saveConfig();
		});
		loadConfig();

	</script>
</body>

</html>
)
