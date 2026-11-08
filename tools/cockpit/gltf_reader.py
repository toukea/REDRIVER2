"""Minimal glTF 2.0 / GLB reader: world-space triangles, UVs, base colour images."""
import io,json,os,struct,numpy as np
CT={5120:np.int8,5121:np.uint8,5122:np.int16,5123:np.uint16,5125:np.uint32,5126:np.float32}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def quat(q):
    x,y,z,w=q
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def local(n):
    if 'matrix' in n: return np.array(n['matrix']).reshape(4,4).T
    m=np.eye(4)
    s=np.diag(n.get('scale',[1,1,1]))
    r=quat(n.get('rotation',[0,0,0,1]))
    m[:3,:3]=r@s; m[:3,3]=n.get('translation',[0,0,0]); return m
def load(path):
    if path.endswith('.glb'):
        b=open(path,'rb').read(); jl=struct.unpack_from('<I',b,12)[0]
        g=json.loads(b[20:20+jl]); bins=[b[20+jl+8:]]; base=os.path.dirname(path)
    else:
        g=json.load(open(path)); base=os.path.dirname(path)
        bins=[open(os.path.join(base,bf['uri']),'rb').read() for bf in g['buffers']]
    def acc(i):
        a=g['accessors'][i]; bv=g['bufferViews'][a['bufferView']]
        dt=CT[a['componentType']]; n=NC[a['type']]
        off=bv.get('byteOffset',0)+a.get('byteOffset',0); stride=bv.get('byteStride',0)
        data=bins[bv.get('buffer',0)]
        if stride and stride!=np.dtype(dt).itemsize*n:
            arr=np.ndarray((a['count'],n),dt,data,off,(stride,np.dtype(dt).itemsize))
        else: arr=np.frombuffer(data,dt,a['count']*n,off).reshape(a['count'],n)
        arr=arr.astype(np.float64)
        if a.get('normalized'): arr/=np.iinfo(dt).max
        return arr
    prims=[]
    def walk(ni,parent):
        n=g['nodes'][ni]; w=parent@local(n)
        if 'mesh' in n:
            for p in g['meshes'][n['mesh']]['primitives']:
                if p.get('mode',4)!=4: continue
                pos=acc(p['attributes']['POSITION']); pos=(np.c_[pos,np.ones(len(pos))]@w.T)[:,:3]
                uv=acc(p['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in p['attributes'] else np.zeros((len(pos),2))
                idx=acc(p['indices']).astype(int).reshape(-1,3) if 'indices' in p else np.arange(len(pos)).reshape(-1,3)
                prims.append({'node':n.get('name',''),'mesh':g['meshes'][n['mesh']].get('name',''),'mat':p.get('material',-1),'pos':pos,'uv':uv,'idx':idx})
        for c in n.get('children',[]): walk(c,w)
    for r in g['scenes'][g.get('scene',0)]['nodes']: walk(r,np.eye(4))
    mats=[]
    for m in g.get('materials',[]):
        pbr=m.get('pbrMetallicRoughness',{})
        img=None
        if 'baseColorTexture' in pbr:
            src=g['textures'][pbr['baseColorTexture']['index']]['source']; im=g['images'][src]
            if 'uri' in im: img=os.path.join(base,im['uri'])
            elif 'bufferView' in im:
                bv=g['bufferViews'][im['bufferView']]; o=bv.get('byteOffset',0)
                img=io.BytesIO(bins[bv.get('buffer',0)][o:o+bv['byteLength']])
        mats.append({'name':m.get('name',''),'color':pbr.get('baseColorFactor',[1,1,1,1]),'image':img,'alpha':m.get('alphaMode','OPAQUE')})
    return prims,mats
if __name__=='__main__':
    import sys,collections
    prims,mats=load(sys.argv[1])
    allp=np.concatenate([p['pos'] for p in prims]); print('bounds',allp.min(0).round(3),allp.max(0).round(3),'tris',sum(len(p['idx']) for p in prims))
    for p in prims:
        m=mats[p['mat']] if p['mat']>=0 else {'name':'-','image':None}
        img=m['image'];img=os.path.basename(img) if isinstance(img,str) else ('embedded' if img else '')
        print(f"{len(p['idx']):>6} {p['node'][:28]:28} {m['name'][:30]:30} {img} c={np.round(p['pos'].mean(0),2)}")
