<?php
// Go through Apache's proxied /api/ route rather than fppd's internal :32322
// port directly - that port isn't a documented interface and isn't guaranteed to
// stay where it is.
$data = file_get_contents('http://127.0.0.1/api/plugin-apis/arcade/controllers');
$targets = json_decode(file_get_contents('http://127.0.0.1/api/models?simple=true'), true);
$controllers = json_decode($data, true);
?>
<script language="Javascript">
allowMultisyncCommands = true;

function RefreshLastMessages() {
    $.get('api/plugin-apis/arcade/events', function (data) {
          $("#lastMessages").text(data);
        }
    );
}
    
function GetJS(button, pb) {
    var js = {};
    js["enabled"] = $("#" + button + "_enabled").is(':checked');
    if (pb) {
        js["pressed"] = {};
        js["released"] = {};
        CommandToJSON(button + "_PressedCommand", "tablePressed" + button, js["pressed"]);
        CommandToJSON(button + "_ReleasedCommand", "tableReleased" + button, js["released"]);
    } else {
        js["command"] = {};
        CommandToJSON(button + "_AxisCommand", "tableAxis" + button, js["command"]);
    }

    return js;
}
function SaveJoystickInputs() {
    var js = Array();
    
<?
    $count = 0;
    foreach(is_array($controllers) ? $controllers : Array() as $controller) {
        $contName = $controller['name'];
        $contNameClean = str_replace("-", "_", $contName);
        $contNameClean = str_replace(":", "_", $contNameClean);
        $contNameClean = str_replace(",", "_", $contNameClean);
        $contNameClean = str_replace(" ", "_", $contNameClean);
	$contNameClean = str_replace(".", "_", $contNameClean);
        for ($x = 0; $x < $controller['buttons']; $x++) {
            $buttonNameClean = $contNameClean . "_" . $x;
            echo "    var jsb = GetJS('" . $buttonNameClean . "', true);\n";
            echo "    jsb['controller'] = " . json_encode($contName) . ";\n";
            echo "    jsb['button'] = " . $x . ";\n";
            echo "    js.push(jsb);\n";
        }
        for ($x = 0; $x < $controller['axis']; $x++) {
            $buttonNameClean = $contNameClean . "_a" . $x;
            echo "    var jsb = GetJS('" . $buttonNameClean . "', false);\n";
            echo "    jsb['controller'] = " . json_encode($contName) . ";\n";
            echo "    jsb['axis'] = " . $x . ";\n";
            echo "    js.push(jsb);\n";
        }
    }
?>
    var postData = JSON.stringify(js, null, 4);
    $.ajax({
            url: "api/configfile/joysticks.json",
            type: 'POST',
            contentType: 'application/json',
            data: postData,
            dataType: 'json',
            success: function(data) {
                    $('html,body').css('cursor','auto');
                    if (data.Status == 'OK') {
                        $.jGrowl("Joystick Inputs Saved.");
                        SetRestartFlag(2);
                    } else {
                        alert('ERROR: ' + data.Message);
                    }
            },
            error: function() {
                    $('html,body').css('cursor','auto');
                    alert('Error, Failed to save Joystick inputs');
            }
    });
    
}
var GPMAPPING = {
    "1": "Fire",
    "8": "Select",
    "9": "Start",
    "11": "Up",
    "12": "Down",
    "13": "Left",
    "14": "Right",
    "a0": "Left -> Right",
    "a1": "Up -> Down"
};
var DPMAPPING = {
    "0": "Up",
    "1": "Down",
    "2": "Right",
    "3": "Left",
    "4": "Down/Left",
    "5": "Down/Right",
    "6": "Up/Left",
    "7": "Up/Right",
    "8": "Select",
    "9": "Start"
};

function SetDefaultsForController() {
    var controllName = $('#ControllerSelect').val();
    var controllType = $('#ControllerType').val();
    var target = $('#Target').val();

    var js = Array();
    js["command"] = "FPP Arcade Button";
    js["multisyncCommand"] = false,
    js["multisyncHosts"] = "",
    js["args"] = Array();
    js["args"].push("Fire - Pressed");
    js["args"].push(target);

    var jsa = Array();
    jsa["command"] = "FPP Arcade Axis";
    jsa["multisyncCommand"] = false,
    jsa["multisyncHosts"] = "",
    jsa["args"] = Array();
    jsa["args"].push("");
    jsa["args"].push(target);
    jsa["args"].push("0");

    var mapping = (controllType == "GP") ? GPMAPPING : DPMAPPING;
    for (var key in mapping) {
        var buttonNameClean = controllName + "_" + key;
        if (key[0] == 'a') {
            var tbName = "tableAxis" + buttonNameClean;
            jsa["args"][0] = mapping[key];
            PopulateExistingCommand(jsa, buttonNameClean + "_AxisCommand", tbName);
        } else {
            var tbName = "tablePressed" + buttonNameClean;
            js["args"][0] = mapping[key] + " - Pressed";
            PopulateExistingCommand(js, buttonNameClean + "_PressedCommand", tbName);
            js["args"][0] = mapping[key] + " - Released";
            tbName = "tableReleased" + controllName + "_" + key;
            PopulateExistingCommand(js, buttonNameClean + "_ReleasedCommand", tbName);
        }
        $("#" + buttonNameClean + "_enabled").prop('checked', true);
    }

}


/////////////////////////////////////////////////////////////////////////////
$(document).ready(function(){
});
</script>

<!-- --------------------------------------------------------------------- -->



<div id="global" class="settings">
<legend>JoyStick/Gamepad Config</legend>
<div class="row">
    <div class="col-auto mr-auto">
        <div classs="row">
            <div class="col-md1">
                <input type="button" value="Save" class="buttons genericButton" onclick="SaveJoystickInputs();">
                <span style="display: inline-block; width: 100px;">&nbsp;</span>
                <select id="ControllerSelect">
                <?
$count = 0;
foreach(is_array($controllers) ? $controllers : Array() as $controller) {
    $contName = $controller['name'];
    $contNameClean = str_replace("-", "_", $contName);
    $contNameClean = str_replace(":", "_", $contNameClean);
    $contNameClean = str_replace(",", "_", $contNameClean);
    $contNameClean = str_replace(" ", "_", $contNameClean);
    $contNameClean = str_replace(".", "_", $contNameClean);
    echo "<option value='" . htmlspecialchars($contNameClean) . "'>" . htmlspecialchars($contName) . "</option>\n";
}
?>
                </select>
                <select id="ControllerType">
                    <option value="GP">Game Pad</option>
                    <option value="DM">Dance Mat</option>
                </select>
                <select id="Target">
                    <option value="" selected></option>
<?
foreach (is_array($targets) ? $targets : Array() as $target) {
    echo "<option value='" . htmlspecialchars($target) . "'>" . htmlspecialchars($target) . "</option>\n";
}
?>
                </select>
                <input type="button" value="Set Defaults" class="buttons genericButton" onclick="SetDefaultsForController();">
            </div>
        </div>
        <div class="row">
            <div class="col-auto">
                <div class='fppTableWrapper'>
                    <div class='fppTableContents'>
                        <table  class="fppSelectableRowTable" id="JoystickInputs"  width='100%'>
                            <thead>
                                <tr>
                                    <th rowspan='2' style="text-align:center">En.</th>
                                    <th rowspan='2' style="text-align:center; margin-right:50px;">Controller</th>
                                    <th rowspan='2' style="text-align:center">Button/Axis</th>
                                    <th colspan='2' style="text-align:center">Commands</th>
                                </tr>
                                <tr>
                                    <th  style="text-align:center">Pressed</th>
                                    <th  style="text-align:center">Released</th>
                                </tr>
                            </thead>
                            <tbody class="ui-sortable">

<?
$count = 0;
foreach(is_array($controllers) ? $controllers : Array() as $controller) {
    $contName = $controller['name'];
    $contNameClean = str_replace("-", "_", $contName);
    $contNameClean = str_replace(":", "_", $contNameClean);
    $contNameClean = str_replace(",", "_", $contNameClean);
    $contNameClean = str_replace(" ", "_", $contNameClean);
    $contNameClean = str_replace(".", "_", $contNameClean);
    for ($x = 0; $x < $controller['buttons']; $x++) {
        $buttonNameClean = $contNameClean . "_" . $x;
        $style = " evenRow";
        if ($count % 2 == 0) {
            $style = " oddRow";
        }
        $count = $count + 1;
        ?>
            <tr class='fppTableRow <?= $style ?>' id='row_<?= $count ?>'>
                <td><input type="checkbox" id="<?= $buttonNameClean ?>_enabled"></td>
                <td style="padding-left:10px;padding-right:20px;"><?= htmlspecialchars($contName) ?></td>
                <td>Button <?= $x ?></td>
                <td>
                    <table border=0 class='fppTable' id='tablePressed<?= $buttonNameClean ?>'>
                    <tr>
        <td></td><td><select id='<?= $buttonNameClean ?>_PressedCommand' onChange='CommandSelectChanged("<?= $buttonNameClean ?>_PressedCommand", "tablePressed<?= $buttonNameClean ?>", false);'><option value=""></option></select></td>
                    </tr>
                    </table>
                </td>
                <td>
                        <table border=0 class='fppTable' id='tableReleased<?= $buttonNameClean ?>'>
                        <tr>
        <td></td><td><select id='<?= $buttonNameClean ?>_ReleasedCommand' onChange='CommandSelectChanged("<?= $buttonNameClean ?>_ReleasedCommand", "tableReleased<?= $buttonNameClean ?>", false);'><option value=""></option></select></td>
                        </tr>
                        </table>
                <script>
                LoadCommandList($('#<?= $buttonNameClean ?>_PressedCommand'));
                LoadCommandList($('#<?= $buttonNameClean ?>_ReleasedCommand'));
                </script>
            </td>
        </tr>
<?
    }
    for ($x = 0; $x < $controller['axis']; $x++) {
        $buttonNameClean = $contNameClean . "_a" . $x;
        $style = " evenRow";
        if ($count % 2 == 0) {
            $style = " oddRow";
        }
        $count = $count + 1;
        ?>
            <tr class='fppTableRow <?= $style ?>' id='row_<?= $count ?>'>
                <td><input type="checkbox" id="<?= $buttonNameClean ?>_enabled"></td>
                <td style="padding-left:10px;padding-right:20px;"><?= htmlspecialchars($contName) ?></td>
                <td>Axis <?= $x ?></td>
                <td colspan="2">
                    <table border=0 class='fppTable' id='tableAxis<?= $buttonNameClean ?>'>
                    <tr>
        <td></td><td><select id='<?= $buttonNameClean ?>_AxisCommand' onChange='CommandSelectChanged("<?= $buttonNameClean ?>_AxisCommand", "tableAxis<?= $buttonNameClean ?>", false);'><option value=""></option></select></td>
                    </tr>
                    </table>
                <script>
                LoadCommandList($('#<?= $buttonNameClean ?>_AxisCommand'));
                </script>
            </td>
        </tr>
<?
    }
}
?>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <div class="col-auto">
        <div>
            <div class="row">
                <div class="col">
                    Last Messages:&nbsp;<input type="button" value="Refresh" class="buttons" onclick="RefreshLastMessages();">
                </div>
            </div>
            <div class="row">
                <div class="col">
                    <pre id="lastMessages" style='min-width:150px; margin:1px;min-height:300px;'></pre>
                </div>
            </div>
        </div>
    </div>
</div>


<script>
<?

if (file_exists($mediaDirectory . '/config/joysticks.json')) {
    $data = file_get_contents($mediaDirectory . '/config/joysticks.json');
    $jsInputJson = json_decode($data, true);
    echo "var joystickConfig = " . json_encode($jsInputJson, JSON_PRETTY_PRINT) . ";\n";

    $x = 0;
    foreach($jsInputJson as $js) {
        $contName = $js['controller'];
        $contNameClean = str_replace("-", "_", $contName);
        $contNameClean = str_replace(":", "_", $contNameClean);
        $contNameClean = str_replace(",", "_", $contNameClean);
        $contNameClean = str_replace(" ", "_", $contNameClean);
        $contNameClean = str_replace(".", "_", $contNameClean);

        if (isset($js['button'])) {
            $button = $js['button'];
            $buttonNameClean = $contNameClean . "_" . $button;
            echo "PopulateExistingCommand(joystickConfig[" . $x . "][\"pressed\"], '" . $buttonNameClean . "_PressedCommand', 'tablePressed" . $buttonNameClean . "', false);\n";
            echo "PopulateExistingCommand(joystickConfig[" . $x . "][\"released\"], '" . $buttonNameClean . "_ReleasedCommand', 'tableReleased" . $buttonNameClean . "', false);\n";
        } else {
            $buttonNameClean = $contNameClean . "_a" . $js['axis'];
            echo "PopulateExistingCommand(joystickConfig[" . $x . "][\"command\"], '" . $buttonNameClean . "_AxisCommand', 'tableAxis" . $buttonNameClean . "', false);\n";
        }

        if ($js['enabled'] == true) {
            echo "$('#" . $buttonNameClean . "_enabled').prop('checked', true);\n";
        }

        $x = $x + 1;
    }
}
?>
RefreshLastMessages();
</script>

	</div>
