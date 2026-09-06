// Exercise the production update status rendering, not a copy of its logic.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const page=readFileSync(new URL('../../src/network/device_page.h',import.meta.url),'utf8');
const script=page.split('<script>')[1].split('</script>')[0];
new vm.Script(script);
const render=script.slice(script.indexOf('let otaRequestPending='),script.indexOf('async function refreshStatus()'));
const versions=script.slice(script.indexOf('function versionParts('),script.indexOf('function showReleaseLink('));
const elements=new Map(),messages=[];
const get=id=>{if(!elements.has(id))elements.set(id,{style:{},textContent:'',disabled:false});return elements.get(id)};
const context=vm.createContext({$:get,state:{releaseChecked:true},performance:{now:()=>5000},toast:m=>messages.push(m)});
vm.runInContext(versions+render,context);
function status(ota,version='1.5.8'){
 context.status={ota:{active:false,preparing:false,progress:0,...ota},firmware:{version}};
 vm.runInContext('renderOtaStatus(status)',context);
}
status({failed:true});
assert.match(get('releaseStatus').textContent,/Update failed/);
assert.match(get('otaMessage').textContent,/previous firmware is retained/);
assert.equal(get('installUpdate').disabled,false);
// Polling during the ready/start gap must not enable a second request.
vm.runInContext('otaRequestPending=true',context);
status({ready:true});assert.equal(get('installUpdate').disabled,true);
vm.runInContext("otaRequestPending=false;githubInstall={tag:'v1.5.9',startedAt:0}",context);
status({active:true,progress:45});
assert.equal(get('installUpdate').disabled,true);
assert.match(get('releaseStatus').textContent,/45%/);
assert.equal(messages.length,0,'acceptance or progress is not success');
// Recovery reboot with the old version must not count as success.
status({});assert.match(get('releaseStatus').textContent,/Update failed/);
assert.equal(messages.length,0);
vm.runInContext("githubInstall={tag:'v1.5.9',startedAt:0}",context);
status({},'1.5.9');
assert.equal(messages.length,1);
assert.match(messages[0],/confirmed/);
assert.equal(context.state.releaseChecked,false,'refresh release comparison after reboot');
console.log('OTA UI status tests passed.');
