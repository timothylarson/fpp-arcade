
<?
function returnIfExists($json, $setting) {
    if ($json == null) {
        return "";
    }
    if (array_key_exists($setting, $json)) {
        return $json[$setting];
    }
    return "";
}

function convertAndGetSettings() {
    global $settings;
        
    $cfgFile = $settings['configDirectory'] . "/plugin.fpp-arcade.json";
    if (file_exists($cfgFile)) {
        $j = file_get_contents($cfgFile);
        $json = json_decode($j, true);
        return $json;
    }
    $j = "{\"games\": []}";
    return json_decode($j, true);
}

$pluginJson = convertAndGetSettings();
?>


<div id="global" class="settings">
<legend>FPP Arcade Config</legend>

<script>

var arcadeConfig = <? echo json_encode($pluginJson, JSON_PRETTY_PRINT); ?>;


function GetTetrisOptions() {
    var html = "Rows: <input type='number' value='20' min='1' max='50' class='option1' data-optionname='Rows'/>&nbsp;";
    html += "Colums: <input type='number' value='11' min='1' max='30' class='option2' data-optionname='Colums'/>&nbsp;";
    html += "Pixel Scaling: <input type='number' value='1' min='1' max='20' class='option3' data-optionname='Pixel Scaling'/>";
    return html;
}
function GetPongOptions() {
    var html = "Pixel Scaling: <input type='number' value='1' min='1' max='20' class='option1' data-optionname='Pixel Scaling'/>&nbsp;";
    html += "Controls: <select class='option2' data-optionname='Controls'>";
    html += "<option value='1'>Up/Down and Left/Right</option>";
    html += "<option value='2'>UpLeft/DownLeft and UpRight/DownRight</option>";
    html += "<option value='3'>Up/Left and Right/Down </option>";
    html += "</select>"
    return html;
}
function GetSnakeOptions() {
    var html = "Pixel Scaling: <input type='number' value='1' min='1' max='20' class='option1' data-optionname='Pixel Scaling'/>";
    return html;
}
function GetBreakoutOptions() {
    var html = "";
    return html;
}
function GetFroggerOptions() {
    var html  = "Pixel Scaling: <input type='number' value='1' min='1' max='20' class='option1' data-optionname='Pixel Scaling'/>&nbsp;";
    html     += "Lanes: <input type='number' value='5' min='1' max='20' class='option2' data-optionname='Lanes'/>&nbsp;";
    html     += "River Speed: <input type='number' value='1' min='1' max='10' class='option4' data-optionname='River Speed'/>&nbsp;";
    html     += "Road Speed: <input type='number' value='1' min='1' max='10' class='option5' data-optionname='Road Speed'/>";
    html     += "Speed Variability (%): <input type='number' value='20' min='0' max='100' class='option6' data-optionname='Speed Variability'/>";
    return html;
}

function GameChanged(sel) {
    var val = $(sel).val();
    var html = "";
    if (val == "Tetris") {
        html = GetTetrisOptions();
    } else if (val == "Pong") {
        html = GetPongOptions();
    } else if (val == "Snake") {
        html = GetSnakeOptions();
    } else if (val == "Breakout") {
        html = GetBreakoutOptions();
    } else if (val == "Frogger") {
        html = GetFroggerOptions();
    }
    $(sel).parent().parent().find(".GameOptions").html(html);
}

var uniqueId = 1;
var modelOptions = "";
function AddArcade() {
    var id = $("#arcadeTableBody > tr").length + 1;
    var html = "<tr class='fppTableRow";
    if (id % 2 != 0) {
        html += " oddRow'";
    }
    html += "'><td class='colNumber rowNumber'>" + id + ".</td><td><input type='checkbox' checked class='enabled'></input><span style='display: none;' class='uniqueId'>" + uniqueId + "</span></td>";
    html += "<td><select class='game' onChange='GameChanged(this);'>";
    html += "<option value='Tetris'>Tetris</option>";
    html += "<option value='Pong'>Pong</option>";
    html += "<option value='Snake'>Snake</option>";
    html += "<option value='Breakout'>Breakout</option>";
    html += "<option value='Frogger'>Frogger</option>";
    html += "</select></td>";
    html += "<td><select class='model'>";
    html += modelOptions;
    html += "</select></td>";
    html += "<td><select class='overlayType'><option value='Overwrite'>Overwrite</option><option value='Transparent'>Transparent</option></select></td>";
    
    html += "<td class='GameOptions'>";
    html += GetTetrisOptions();
    html += "</td>"
    html += "</tr>";
    
    $("#arcadeTableBody").append(html);

    newRow = $('#arcadeTableBody > tr').last();
    $('#arcadeTableBody > tr').removeClass('selectedEntry');
    DisableButtonClass('deleteEventButton');
    uniqueId++;

    return newRow;
}

function SaveGame(row) {
    var enabled = $(row).find('.enabled').is(":checked");
    var game = $(row).find('.game').val();
    var model = $(row).find('.model').val();
    var overlay = $(row).find('.overlayType').val();

    var options = [];
    var i = 0;
    for (var x = 1; x <= 20; x++) {
        var v = $(row).find('.option' + x);
        if (typeof v != "undefined") {
            var name = v.data('optionname');
            if (typeof name != "undefined") {
                var value = v.val();
                options[i++] = {
                    "name": name,
                    "value": value
                };
            }
        }
    }

    var json = {
        "enabled": enabled,
        "game": game,
        "model": model,
        "overlay": overlay,
        "options": options
    };
    return json;
}

function SaveArcade() {
    var arcadeConfig = { "games" : [] };
    var i = 0;
    $("#arcadeTableBody > tr").each(function() {
        arcadeConfig["games"][i++] = SaveGame(this);
    });
    
    var data = JSON.stringify(arcadeConfig);
    $.ajax({
        type: "POST",
        url: 'api/configfile/plugin.fpp-arcade.json',
        dataType: 'json',
        async: false,
        data: data,
        processData: false,
        contentType: 'application/json',
        success: function (data) {
           SetRestartFlag(2);
        }
    });
}


function RenumberRows() {
    var id = 1;
    $('#arcadeTableBody > tr').each(function() {
        $(this).find('.rowNumber').html('' + id++ + '.');
        $(this).removeClass('oddRow');

        if (id % 2 != 0) {
            $(this).addClass('oddRow');
        }
    });
}
function RemoveArcade() {
    if ($('#arcadeTableBody').find('.selectedEntry').length) {
        $('#arcadeTableBody').find('.selectedEntry').remove();
        RenumberRows();
    }
    DisableButtonClass('deleteEventButton');
}


$(document).ready(function() {
                  
    $('#arcadeTableBody').sortable({
        update: function(event, ui) {
            RenumberRows();
        },
        item: '> tr',
        scroll: true
    }).disableSelection();

    $('#arcadeTableBody').on('mousedown', 'tr', function(event,ui){
        $('#arcadeTableBody tr').removeClass('selectedEntry');
        $(this).addClass('selectedEntry');
        EnableButtonClass('deleteEventButton');
    });
});

</script>
<div>
<table border=0>
<tr><td colspan='2'>
        <input type="button" value="Save" class="buttons genericButton" onclick="SaveArcade();">
        <input type="button" value="Add" class="buttons genericButton" onclick="AddArcade();">
        <input id="delButton" type="button" value="Delete" class="deleteEventButton disableButtons genericButton" onclick="RemoveArcade();">
    </td>
</tr>
</table>

<div class='fppTableWrapper fppTableWrapperAsTable'>
<div class='fppTableContents'>
<table class="fppTable" id="arcadeTable"  width='100%'>
<thead><tr class="fppTableHeader"><th>#</th><th>Enabled</th><th>Game</th><th>Model</th><th>Overlay</th><th>Options</th></tr></thead>
<tbody id='arcadeTableBody'>
</tbody>
</table>
</div>
</div>

</div>

<script>
$.ajax({
        url: "api/models",
        type: 'GET',
        async: false,
        contentType: 'application/json',
        success: function(data) {
            data.forEach( function(item, index) {
                modelOptions += "<option value='" + item["Name"] + "'>" + item["Name"] + "</option>\n";
            });
        },
        error: function() {
        }
});
                  


$.each(arcadeConfig["games"], function( key, val ) {
    var row = AddArcade();
    $(row).find('.enabled').prop('checked', val["enabled"]);
    $(row).find('.model').val(val["model"]);
    $(row).find('.game').val(val["game"]);
       
    if (val["overlay"] != null) {
       $(row).find('.overlayType').val(val["overlay"]);
    }
    GameChanged($(row).find('.game'));

    for (var i = 0; i < val['options'].length; i++) {
       for (var y = 1; y <= 20; y++) {
            var v = $(row).find('.option' + y);
           if (typeof v != "undefined") {
               var name = v.data('optionname');
                if (typeof name != "undefined") {
                    if (name == val["options"][i]["name"]) {
                        v.val(val["options"][i]["value"]);
                    }
                }
            }
       }
    }
});
</script>
</div>
