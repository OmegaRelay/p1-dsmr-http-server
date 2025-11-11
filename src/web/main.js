
function initCollapsableSections() {
    var coll = document.getElementsByClassName("collapsible");
    var i;

    for (i = 0; i < coll.length; i++) {
        coll[i].addEventListener("click", function() {
            this.classList.toggle("active");
            var content = this.nextElementSibling;
            if (content.style.display === "block") {
                content.style.display = "none";
            } else {
                content.style.display = "block";
            }
        });
    }
}

async function fetchConfig() {
	try {
		const response = await fetch("/config");
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}

		const json = await response.json();
		const wifi_ssid = document.getElementById("wifi_ssid");
		const wifi_psk = document.getElementById("wifi_psk");
		wifi_ssid.value = json.wifi.ssid
		wifi_psk.value = json.wifi.psk
	} catch (error) {
		console.error(error.message);
	}
}

async function postWifiConfig() {
	try {
		const wifi_ssid = document.getElementById("wifi_ssid");
		const wifi_psk = document.getElementById("wifi_psk");
		const payload = JSON.stringify({
            "wifi": {
                "ssid": wifi_ssid.value,
                "psk": wifi_psk.value,
            }
        });

		const response = await fetch("/config", {
            method : "POST", 
            headers : {
                "Content-Type": "application/json"
            },
            body : payload
        });
		if (!response.ok) {
			throw new Error(`Response satus: ${response.status}`);
		}
	} catch (error) {
		console.error(error.message);
	}
}

window.addEventListener("DOMContentLoaded", (ev) => {
    initCollapsableSections();
    fetchConfig();

	const wifi_save_btn = document.getElementById("wifi_save");
	wifi_save_btn.addEventListener("click", (event) => {
		console.log("wifi config saved");
		postWifiConfig();
	})
})
