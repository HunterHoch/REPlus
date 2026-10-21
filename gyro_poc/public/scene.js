import * as THREE from '/vendor/three.module.js';
import { positionAt, DURATION } from './path.js';

export function createScene(container) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color('#dedbd3');
  scene.fog = new THREE.Fog('#dedbd3', 22, 65);
  const camera = new THREE.PerspectiveCamera(55, 16/9, 0.05, 100);
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  container.prepend(renderer.domElement);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x797267, 2.5));
  const sun = new THREE.DirectionalLight(0xffffff, 3);
  sun.position.set(-5, 10, 7);
  scene.add(sun);
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(150,150), new THREE.MeshStandardMaterial({ color: '#ccc6b9', roughness: 1 }));
  floor.rotation.x = -Math.PI / 2;
  floor.position.y = -0.015;
  scene.add(floor);
  scene.add(new THREE.GridHelper(60,60,0x918778,0xb3aa9c));
  const box = new THREE.Mesh(new THREE.BoxGeometry(1.6,2,1.6), new THREE.MeshStandardMaterial({ color: '#b45309', roughness: .8 }));
  box.position.y = 1;
  scene.add(box);
  const edges = new THREE.LineSegments(new THREE.EdgesGeometry(box.geometry),new THREE.LineBasicMaterial({color:0x70360c}));
  edges.position.copy(box.position);
  scene.add(edges);
  for (const [x,z] of [[-5,-4],[5,-4],[-10,1],[10,1]]) {
    const post = new THREE.Mesh(new THREE.BoxGeometry(.35,3,.35),new THREE.MeshStandardMaterial({color:0x77756d}));
    post.position.set(x,1.5,z);
    scene.add(post);
  }
  const points = Array.from({length:101},(_,i) => { const [x,,z] = positionAt(i/100*DURATION); return new THREE.Vector3(x,.025,z); });
  scene.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:0x496d5c})));
  const target = new THREE.Vector3(0,1,0);
  function aimAtSubject(t) {
    camera.position.fromArray(positionAt(t));
    camera.lookAt(target);
    return camera.quaternion.toArray();
  }
  const initialAim = aimAtSubject(0);
  const observer = new ResizeObserver(() => {
    renderer.setSize(container.clientWidth,container.clientHeight,false);
    camera.aspect = 16/9;
    camera.updateProjectionMatrix();
  });
  observer.observe(container);
  const ctx = document.getElementById('path-map').getContext('2d');
  function drawMap(t) {
    ctx.clearRect(0,0,160,110);
    ctx.strokeStyle = '#8fa899'; ctx.lineWidth = 2; ctx.beginPath();
    const point = time => { const [x,,z] = positionAt(time); return [80+x*8,10+z*10]; };
    for(let i=0;i<=60;i++){const p=point(i/60*DURATION); if(i===0)ctx.moveTo(...p);else ctx.lineTo(...p);}
    ctx.stroke(); ctx.fillStyle = '#d28036'; ctx.fillRect(74,4,12,12);
    ctx.fillStyle = '#fff';ctx.beginPath();ctx.arc(...point(t),4,0,Math.PI*2);ctx.fill();
  }
  return {initialAim, aimAtSubject, draw(t,q) {
    camera.position.fromArray(positionAt(t));
    camera.quaternion.fromArray(q || initialAim);
    renderer.render(scene,camera);
    drawMap(t);
  }};
}
