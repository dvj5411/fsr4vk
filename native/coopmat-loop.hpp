#pragma once
// Included inside fsr4coop after Instruction/Code/emit. Recognizes only the
// four-channel constant-weight loop emitted by the captured 4.1.1 compiler.
// Hoist its four dot16 products, leaving accumulation, stores and loop control
// intact. Twelve zero columns pad the existing 16-column cooperative tile.
inline void expose_constant_dot_loops(Code& code, uint32_t& bound) {
    std::map<uint32_t,size_t> defs, labels;
    std::map<uint32_t,uint32_t> constants;
    std::map<uint32_t,unsigned> refs;
    uint32_t u=0,v4=0,zero=0;
    for(size_t n=0;n<code.size();++n) {
        const auto& i=code[n];
        if(i.op==21 && i.a==std::vector<uint32_t>{i.a[0],32,0})u=i.a[0];
        if(i.op==248)labels[i.a[0]]=n;
        // Only opcodes used by this recognizer have result IDs indexed here.
        if(i.op==43 || i.op==59 || i.op==61 || i.op==65 || i.op==128 ||
           i.op==132 || i.op==170 || i.op==245 || i.op==81)
            defs[i.a[1]]=n;
        for(auto a:i.a)++refs[a];
    }
    if(!u)return;
    for(const auto& i:code) {
        if(i.op==43 && i.a.size()==3 && i.a[0]==u) {
            constants[i.a[1]]=i.a[2];if(!i.a[2])zero=i.a[1];
        }
        if(i.op==23 && i.a==std::vector<uint32_t>{i.a[0],u,4})v4=i.a[0];
    }
    if(!zero || !v4)return;
    const auto def=[&](uint32_t id)->const Instruction* {
        auto f=defs.find(id);return f==defs.end()?nullptr:&code[f->second];
    };
    const auto literal=[&](uint32_t id,uint32_t value) {
        auto f=constants.find(id);return f!=constants.end() && f->second==value;
    };
    std::map<size_t,Code> insert,replace;
    for(size_t h=1;h<code.size();++h) {
        if(code[h].op!=248 || code[h-1].op!=249 || code[h-1].a[0]!=code[h].a[0])continue;
        size_t end=h+1;while(end<code.size() && code[end].op!=248)++end;
        if(end<h+5 || code[end-2].op!=246 || code[end-1].op!=250)continue;
        const auto& merge=code[end-2];const auto& branch=code[end-1];
        if(merge.a.size()!=3 || branch.a[1]!=merge.a[0])continue;
        const auto condition=def(branch.a[0]);
        if(!condition || condition->op!=170 || !literal(condition->a[3],4))continue;
        const auto next=def(condition->a[2]);
        if(!next || next->op!=128 || !literal(next->a[3],1))continue;
        const auto counter=def(next->a[2]);
        if(!counter || counter->op!=245 || counter->a.size()!=6 ||
           !literal(counter->a[2],0) || counter->a[4]!=next->a[1] || counter->a[5]!=merge.a[1])continue;
        auto p=labels.find(counter->a[3]),c=labels.find(merge.a[1]),body=labels.find(branch.a[2]);
        if(p==labels.end() || p->second>=h || c==labels.end() || body==labels.end())continue;
        // Exactly the trivial body -> continue -> header CFG, no extra exits.
        if(body->second+2>=code.size() || code[body->second+1].op!=249 ||
           code[body->second+1].a[0]!=merge.a[1] || code[body->second+2].op!=248)continue;
        size_t ce=c->second+1;while(ce<code.size() && code[ce].op!=248)++ce;
        if(code[ce-1].op!=249 || code[ce-1].a[0]!=code[h].a[0])continue;
        size_t dot=h+1;while(dot<end && code[dot].op!=4450)++dot;
        if(dot+8>=end)continue;
        std::array<uint32_t,4> inputs{};
        std::array<uint32_t,16> weights{};
        uint32_t table=0;bool ok=true;
        for(unsigned k=0;k<4 && ok;++k) {
            const auto& d=code[dot+2*k];const auto& add=code[dot+2*k+1];
            if(d.op!=4450 || d.a.size()!=5 || d.a[0]!=u || d.a[4]!=0 ||
               add.op!=128 || add.a.size()!=4 || add.a[0]!=u || add.a[3]!=d.a[1] ||
               refs[d.a[1]]!=2 || (k && add.a[2]!=code[dot+2*k-1].a[1]) ||
               (k<3 && refs[add.a[1]]!=2)){ok=false;break;}
            inputs[k]=d.a[2];
            // Input value must have been defined before this preheader branch.
            if(!defs.count(inputs[k]) || defs.at(inputs[k])>=h-1){ok=false;break;}
            const auto phi=def(d.a[3]);
            if(!phi || phi->op!=245 || phi->a.size()!=6 || phi->a[3]!=counter->a[3] ||
               phi->a[5]!=merge.a[1] || !constants.count(phi->a[2])){ok=false;break;}
            const auto load=def(phi->a[4]);
            const auto ptr=load && load->op==61 ? def(load->a[2]):nullptr;
            const auto index=ptr && ptr->op==65 && ptr->a.size()==4 ? def(ptr->a[3]):nullptr;
            const auto mul=index && index->op==128 && literal(index->a[2],k) ? def(index->a[3]):nullptr;
            if(!mul || mul->op!=132 || mul->a[2]!=next->a[1] || !literal(mul->a[3],4)) {ok=false;break;}
            if(table && table!=ptr->a[2]){ok=false;break;}table=ptr->a[2];
            weights[k]=phi->a[2];
        }
        const auto variable=def(table);
        if(!ok || !variable || variable->op!=59 || variable->a[2]!=7)continue; // Function storage
        std::map<uint32_t,uint32_t> stored;
        // Require all 16 literal weights assigned in this single preheader.
        for(size_t n=p->second+1;n<h-1;++n)if(code[n].op==62) {
            const auto ptr=def(code[n].a[0]);
            if(ptr && ptr->op==65 && ptr->a.size()==4 && ptr->a[2]==table) {
                if(!constants.count(ptr->a[3]) || !constants.count(code[n].a[1])){ok=false;break;}
                stored[constants.at(ptr->a[3])]=code[n].a[1];
            }
        }
        // No stores or pointer escape in the recognized loop. The sole output
        // store is to a distinct Function array, never the weight array.
        for(size_t n=h+1;n<ce && ok;++n) {
            const auto& i=code[n];
            if(i.op==62) {
                const auto ptr=def(i.a[0]);
                if(!ptr || ptr->op!=65 || ptr->a[2]==table)ok=false;
            }
            if(i.op==57 || i.op==63 || i.op==64)ok=false;
        }
        for(unsigned n=0;n<16 && ok;++n) {
            if(!stored.count(n) || (n<4 && constants.at(stored[n])!=constants.at(weights[n])))ok=false;
            else weights[n]=stored[n];
        }
        if(!ok || insert.count(h-1))continue;
        Code hoist;std::array<uint32_t,4> values{};
        for(unsigned row=0;row<16;++row) {
            uint32_t acc=zero;
            for(unsigned k=0;k<4;++k) {
                auto d=bound++,sum=bound++;
                emit(hoist,4450,{u,d,inputs[k],row<4?weights[row*4+k]:zero,0});
                emit(hoist,128,{u,sum,acc,d});acc=sum;
            }
            if(row<4)values[row]=acc;
        }
        auto vector=bound++;
        emit(hoist,80,{v4,vector,values[0],values[1],values[2],values[3]});
        insert[h-1]=std::move(hoist);
        Code replacement;auto value=bound++;
        emit(replacement,77,{u,value,vector,counter->a[1]});
        emit(replacement,128,{u,code[dot+7].a[1],code[dot+1].a[2],value});
        replace[dot]=std::move(replacement);
    }
    if(insert.empty())return;
    Code result;
    for(size_t n=0;n<code.size();) {
        if(insert.count(n))result.insert(result.end(),insert[n].begin(),insert[n].end());
        if(replace.count(n)){result.insert(result.end(),replace[n].begin(),replace[n].end());n+=8;}
        else result.push_back(code[n++]);
    }
    code=std::move(result);
}
