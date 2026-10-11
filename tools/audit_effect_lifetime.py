"""Verify the pinned effect allocation/release sites; never modify the game."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import pefile
from audit_update_entry import PIN


def audit(data):
    if hashlib.sha256(data).hexdigest() != PIN:
        raise ValueError('Unsupported executable; no lifetime-site assertions made')
    pe = pefile.PE(data=data)

    def exact(rva, expected):
        if pe.get_data(rva, len(expected)) != expected:
            raise ValueError(f'Instruction mismatch at {rva:#x}')

    def relative_call(rva, target):
        code = pe.get_data(rva, 5)
        if code[0] != 0xe8 or rva + 5 + struct.unpack_from('<i', code, 1)[0] != target:
            raise ValueError(f'Call mismatch at {rva:#x}')

    def lea(rva, prefix, target):
        code = pe.get_data(rva, 7)
        if code[:3] != prefix or rva + 7 + struct.unpack_from('<i', code, 3)[0] != target:
            raise ValueError(f'Type reference mismatch at {rva:#x}')

    allocations = []
    for size_site, size, call, type_site, prefix, vtable, store_site, store in (
        (0x334A14, 0x1E0, 0x334A19, 0x334989, b'\x4c\x8d\x3d', 0x3E88CE8, 0x334A5B, b'\x4c\x89\x38'),
        (0x2FC70E, 0x180, 0x2FC71B, 0x2FC767, b'\x48\x8d\x05', 0x3E88D18, 0x2FC76E, b'\x48\x89\x03'),
        (0x33450C, 0x130, 0x334511, 0x33455C, b'\x48\x8d\x05', 0x3E88C88, 0x334563, b'\x48\x89\x01'),
        (0x3345AA, 0x130, 0x3345AF, 0x33461A, b'\x48\x8d\x05', 0x3E88CB8, 0x334621, b'\x48\x89\x06'),
    ):
        exact(size_site, b'\xb9' + struct.pack('<I', size))
        relative_call(call, 0x4A61C0)
        lea(type_site, prefix, vtable)
        exact(store_site, store)
        allocations.append(dict(call_rva=call, return_rva=call+5,
                                requested_bytes=size, eventual_vtable_rva=vtable,
                                type_store_rva=store_site))

    # This full contiguous span includes both neighbor-link repairs and head repair.
    exact(0x3340B7, bytes.fromhex(
        '488b4b184889334885c97408488b431048894110488b4b104885c97408'
        '488b431848894118488b4b284839197507488b4310488901ba70000000488bcb'))
    relative_call(0x3340F4, 0x1F90000)
    lea(0x334072, b'\x48\x8d\x35', 0x3E88D48)
    # Release dispatch discards the call-site EDX value and forwards the pointer.
    exact(0xD46A15, bytes.fromhex('498b00488bd1498bc848ff6020'))
    # RIP-relative external roots used by the inspected effect helpers.
    environment_roots = []
    for site, prefix, target in (
        (0x3300C4, b'\x0f\xb6\x15', 0x470DEDC),
        (0x3300CB, b'\x48\x8d\x0d', 0x4845218),
        (0x3300E0, b'\x4c\x8d\x05', 0x48462C8),
        (0x3300FF, b'\x48\x8d\x0d', 0x4844DD0),
        (0x330106, b'\x48\x8d\x05', 0x4845E80),
        (0x331184, b'\x48\x8b\x05', 0x470E038),
        (0x391370, b'\x48\x8b\x05', 0x40FBC38),
        (0x391394, b'\x48\x8b\x05', 0x40F7688),
    ):
        lea(site, prefix, target)
        environment_roots.append(dict(instruction_rva=site, target_rva=target))
    query_helpers = []
    for site, target, role in (
        (0x391404, 0x390E90, 'Search stage cell interval records'),
        (0x3914CA, 0x391270, 'Test query point against candidate triangle'),
        (0x3914FB, 0x32FAA0, 'Decode packed candidate scalar'),
        (0x39157A, 0x390910, 'Intersect query segment with candidate plane'),
    ):
        relative_call(site, target)
        query_helpers.append(dict(call_rva=site, target_rva=target, inferred_role=role))
    # Actual dependent pointer loads, rather than assuming the query root owns
    # one contiguous allocation. These require a separate lifetime contract.
    for site, code in (
        (0x3913F7, '488b4cc108'),
        (0x3913FC, '4c8b01'),
        (0x39142C, '498b0cc0'),
        (0x391460, '4d8b02'),
    ):
        exact(site, bytes.fromhex(code))
    for site, code in (
        (0x391DA1, '41ff5128'), (0x391DBF, '41ff5128'), (0x391DE7, 'ff5028'),
        (0x391F11, '41ff5130'), (0x391F2F, '41ff5130'), (0x391F57, 'ff5030'),
    ):
        exact(site, bytes.fromhex(code))
    relative_call(0x38443B, 0x391CA0)
    relative_call(0x38479E, 0x391E10)
    relative_call(0x3844A7, 0x301820)
    relative_call(0x3847F7, 0x301B10)
    relative_call(0x392096, 0x2F79B0)
    relative_call(0x314529, 0x392080)
    relative_call(0x37CD7F, 0x37C630)
    relative_call(0x37C677, 0x37C740)
    relative_call(0x37C763, 0x4A61C0)
    exact(0x37C75E, bytes.fromhex('b920000000'))
    # Strong-count disposal calls the stored deleter on the stored buffer.
    exact(0x3E89308, struct.pack('<QQQ', pe.OPTIONAL_HEADER.ImageBase+0x37C7B0,
          pe.OPTIONAL_HEADER.ImageBase+0x2DE1D0, pe.OPTIONAL_HEADER.ImageBase+0x348F50))
    exact(0x37C620, bytes.fromhex('48ff25c90ceb02'))
    free_imports = [imp for entry in pe.DIRECTORY_ENTRY_IMPORT for imp in entry.imports
                    if imp.address-pe.OPTIONAL_HEADER.ImageBase == 0x322D2F0]
    if len(free_imports) != 1 or free_imports[0].name != b'free':
        raise ValueError('Stage buffer deleter is not the expected free import')
    exact(0x37C7C0, bytes.fromhex('488b4918ff5010'))
    exact(0x2DE1D8, bytes.fromhex('ba0100000048ff6010'))
    exact(0x348F68, bytes.fromhex('ba20000000'))
    relative_call(0x348F6D, 0x1F90000)
    lea(0x3144AF, b'\x48\x8b\x05', 0x470D0D0)
    lea(0x3144BA, b'\x48\x8b\x05', 0x470D0F8)
    # Leaf setter has no unwind entry; pointer copies do not acquire ownership.
    exact(0x2DAE70, bytes.fromhex('488b01'))
    lea(0x2DAE73, b'\x48\x89\x05', 0x470D0D0)
    exact(0x2DAE7A, bytes.fromhex('488b4108'))
    lea(0x2DAE7E, b'\x48\x89\x05', 0x470D0F8)
    exact(0x3CFC20, bytes.fromhex('488bcae948b2f0ff'))
    lea(0x3DCC60, b'\x48\x8d\x05', 0x327B610)
    lea(0x441396, b'\x48\x8d\x05', 0x327B610)
    exact(0x327B650, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0x3CFC20))
    relative_call(0x3FA4D5, 0x420D80)
    relative_call(0x420D97, 0x4208B0)
    relative_call(0x4209C7, 0x89D3A0)
    exact(0x420B1F, bytes.fromhex('488b46084989442408'))
    exact(0x420B2A, bytes.fromhex('488b460849890424'))
    exact(0x89D3D7, bytes.fromhex('488d9908020000'))
    exact(0x89D5D5, bytes.fromhex('4c8b78104c8b6808'))
    exact(0x89D5EB, bytes.fromhex('f041ff4708f041ff4708'))
    exact(0x89D79A, bytes.fromhex('ff12'))
    # At the accessor call, the enumerator still owns its temporary strong
    # references. Pin its frame layout before using any caller-stack evidence.
    # Entry RSP=S; body RSP=S-0x158; RBP=S-0x58. Control is at
    # RBP-0x80 = body RSP+0x80, or accessor return-address slot+0x88.
    exact(0x89D3A0, bytes.fromhex(
        '488bc4488950105556574154488d68a84881ec38010000'))
    exact(0x89D5DD, bytes.fromhex('4c896c24784c897d80'))
    exact(0x89D79C, bytes.fromhex('488945b0'))
    exact(0x89D849, bytes.fromhex('4d85ff746c83c8fff0410fc14708'))
    # This compiler helper does not publish an embedded weak owner pointer.
    exact(0x39AE30, bytes.fromhex(
        '48894c240848895424104c894424184c894c2420c3'))
    exact(0x89D830, bytes.fromhex('48894810'))
    # Container clear holds the same owner+0x208 critical section and drops
    # each entry's strong reference; concrete payload disposal remains unknown.
    exact(0x899122, bytes.fromhex('4881c108020000'))
    exact(0x899129, bytes.fromhex('ff1569359902'))
    exact(0x89913A, bytes.fromhex('ff1550359902'))
    exact(0x899149, bytes.fromhex('488d8f00010000'))
    relative_call(0x899150, 0x1C14E80)
    exact(0x899225, bytes.fromhex('488d8f08020000'))
    exact(0x899243, bytes.fromhex('48ff253e349902'))
    exact(0x1C14EC0, bytes.fromhex('488b5b10'))
    exact(0x1C14ECC, bytes.fromhex('f00fc14308'))
    exact(0x1C14EDC, bytes.fromhex('ff10'))
    exact(0x1C14EE1, bytes.fromhex('f00fc1430c'))
    exact(0x1C14EF6, bytes.fromhex('ff5008'))
    exact(0x8A1D45, bytes.fromhex('488d9908020000'))
    exact(0x8A1D60, bytes.fromhex('ff1532a99802'))
    exact(0x8A1D6D, bytes.fromhex('ff151da99802'))
    exact(0x8A1E57, bytes.fromhex('498d8c2400010000'))
    relative_call(0x8A1E5F, 0x863A60)
    exact(0x8A24F8, bytes.fromhex('ff158aa19802'))
    relative_call(0x898270, 0x897E70)
    exact(0x898279, bytes.fromhex('c683a001000001'))
    relative_call(0x2DDC354, 0x2DDB8C0)
    relative_call(0x2DDB968, 0x2DD9BB0)
    lea(0x2DD9BCE, b'\x48\x8d\x05', 0x3CEA0F8)
    lea(0x2DDB983, b'\x48\x8d\x0d', 0x36E9D40)
    exact(0x3CEA0F8, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0x2DDDA30))
    exact(0x3CEA130, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0x2DDB220))
    exact(0x36E9D40, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0x29B9940))
    exact(0x36E9D48, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0x216D8C0))
    exact(0x29B9940, bytes.fromhex('488b49104885c9740c488b01ba0100000048ff6038c3'))
    exact(0x216D8D3, bytes.fromhex('f6c201740aba18000000'))
    relative_call(0x216D8DD, 0x1F90000)
    exact(0x2DDDA43, bytes.fromhex('488b4338'))
    relative_call(0x2DDB22F, 0x2DDA4E0)
    exact(0x2DDA58E, bytes.fromhex('e9adfeffff'))
    exact(0x2DDA48A, bytes.fromhex('488b4b38'))
    relative_call(0x2DDA493, 0xD46A00)
    relative_call(0x2DDADA7, 0x2DDBB10)
    exact(0x2DDBEFA, bytes.fromhex('498d4d38'))
    relative_call(0x2DDBEFE, 0x1F7DC10)
    exact(0x2DDBF44, bytes.fromhex('498d4d38'))
    relative_call(0x2DDBF48, 0x11E1B10)
    exact(0x2DDBF53, bytes.fromhex('498b4d38'))
    relative_call(0x2DDBF57, 0x311C070)
    exact(0x2DD9BCA, bytes.fromhex('c6435000'))
    exact(0x2DDAA33, bytes.fromhex('41c6405001'))
    exact(0x2DDC800, bytes.fromhex('e9dbe1ffff'))
    exact(0x2DDC850, bytes.fromhex('e98be4ffff'))
    lea(0x2DE16EE, b'\x48\x8d\x05', 0x2DDC800)
    lea(0x2DE174E, b'\x48\x8d\x05', 0x2DDC850)
    relative_call(0x2DDB9FD, 0x2DE1670)
    relative_call(0x2DE176A, 0xE69090)
    lea(0xE69191, b'\x48\x8d\x05', 0x35319C0)
    exact(0x35319C0, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0xE71380))
    exact(0xE713B9, bytes.fromhex('ff5310'))
    relative_call(0xE713C3, 0xE771C0)
    exact(0xE713D8, bytes.fromhex('48ff6010'))
    exact(0xE771C6, bytes.fromhex('c6415801'))
    exact(0xE7722B, bytes.fromhex('48ffe0'))
    exact(0x2DDC840, bytes.fromhex('488b09e9d8320000'))
    relative_call(0x2DDBA2C, 0x2DE0C80)
    exact(0x2DE0CAB, bytes.fromhex('f0ff400c'))
    relative_call(0x2DE0CCB, 0x2DD8C80)
    exact(0x2DDE236, bytes.fromhex('f00fb14a08'))
    exact(0x2DD8D17, bytes.fromhex('488b4cc818'))
    exact(0x2DD8D21, bytes.fromhex('f00fc1690c'))
    exact(0x2DD8D35, bytes.fromhex('ff5008'))
    relative_call(0x2DD8D49, 0xD46A00)
    exact(0x2DD8D4E, bytes.fromhex('41b820000000'))
    exact(0x3E893B8+0x78, struct.pack('<Q', pe.OPTIONAL_HEADER.ImageBase+0x37CD60))
    for site, code in (
        (0x39208F, '4881c130040000'), (0x3920D1, '48893b'),
        (0x392138, '488d9330040000'), (0x392142, 'ff5078'),
        (0x39214D, '488b8b30040000'), (0x392154, '48638340040000'),
        (0x39215E, '48898b18040000'), (0x392165, '48898b28040000'),
        (0x39219A, '488d7308'), (0x3921B4, '4c8b9b28040000'),
        (0x3921D6, '48898328040000'), (0x392223, '4c891e'),
        (0x39222F, '4883c608'), (0x392308, '4989440af8'),
        (0x3923BB, '4c89bb08040000'),
        (0x37C672, '488d4c2428'), (0x37C67D, '488b4c2430'),
        (0x37C687, 'f0ff4108'), (0x37C699, '48894e08'),
        (0x37C6A2, '488906'), (0x37C6E2, '48896e10'),
        (0x37C6ED, 'f00fc14108'),
        (0x2F79E0, 'f00fc14208'), (0x2F79F5, 'ff10'),
        (0x2F79F7, 'f00fc17b0c'), (0x2F7A09, 'ff5008'),
    ):
        exact(site, bytes.fromhex(code))
    relative_call(0x37C677, 0x37C740)
    relative_call(0x37CD7F, 0x37C630)
    for site, code in (
        (0x301D26, '488b14c8'), (0x301D34, '4803c2'),
        (0x301D60, '4c8b04d1'), (0x301D6E, '4903c8'),
        (0x301D37, '48890592215404'), (0x301D71, '48890d60215404'),
    ):
        exact(site, bytes.fromhex(code))
    lea(0x38441B, b'\x48\x8d\x0d', 0x4844DD0)
    lea(0x384422, b'\x48\x8d\x05', 0x4845E80)
    lea(0x38477E, b'\x48\x8d\x0d', 0x4844DD0)
    lea(0x384785, b'\x48\x8d\x05', 0x4845E80)
    return dict(schema=1, executable_sha256=PIN, game_modified=False,
                allocation_sites=allocations,
                effect_environment_roots=environment_roots,
                stage_query_helpers=query_helpers,
                stage_data_lifetime_verified=False,
                stage_query_resource=dict(setup_rva=0x392080,
                    release_handle_rva=0x2F79B0, handle_offset=0x430,
                    control_pointer_offset=0x438, allocation_interface_slot=0x78,
                    generated_buffer_size_offset=0x440,
                    generated_table_offset=8, generated_candidate_array_offset=0x408,
                    generated_table_points_into_owned_buffer=True,
                    generated_cells_reference_descriptor_payload=True,
                    setup_mutates_descriptor_pointer_fields=True,
                    generated_handle_safe_acquisition_verified=False,
                    generated_owner_candidate=dict(
                        constructor_rva=0x37C740, caller_return_rva=0x37C67C,
                        temporary_pair_offset_from_allocator_rsp=0x28,
                        temporary_control_offset_from_allocator_rsp=0x30,
                        destination_pair_register='RSI',
                        temporary_strong_reference_precedes_publication=True,
                        destination_size_bytes=8,
                        stage_filter_and_thread_handoff_verified=False),
                    allocation_implementation_rva=0x37CD60,
                    handle_builder_rva=0x37C630, control_builder_rva=0x37C740,
                    control_bytes=32, descriptor_global_rvas=[0x470D0D0, 0x470D0F8],
                    descriptor_setter_rva=0x2DAE70,
                    descriptor_forwarder_rva=0x3CFC20,
                    descriptor_interface_vtable_rva=0x327B610,
                    descriptor_interface_slot=0x40,
                    descriptor_producer_rva=0x4208B0,
                    resource_list_rva=0x89D3A0,
                    resource_accessor_return_rva=0x89D79C,
                    resource_accessor_control_from_return_slot=0x88,
                    resource_accessor_object_from_return_slot=0x80,
                    resource_accessor_owner_live_validation_required=True,
                    resource_self_owner_helper_is_noop=True,
                    resource_list_lock_offset=0x208,
                    resource_entry_stride=32, resource_entry_object_offset=8,
                    resource_entry_control_offset=16,
                    returned_descriptor_ownership_verified=False,
                    resource_clear_rva=0x899110,
                    resource_clear_entries_rva=0x1C14E80,
                    resource_clear_uses_enumeration_lock=True,
                    all_resource_mutations_verified=False,
                    resource_population_rva=0x8A1D20,
                    resource_population_uses_enumeration_lock=True,
                    resource_ready_callback_rva=0x898230,
                    resource_factory_rva=0x2DDB8C0,
                    resource_vtable_rva=0x3CEA0F8,
                    resource_control_vtable_rva=0x36E9D40,
                    resource_payload_offset=0x38,
                    resource_payload_destructor_rva=0x2DDA440,
                    resource_payload_mutation_coverage_verified=False,
                    resource_payload_loader_rva=0x2DDBB10,
                    resource_loader_callback_rva=0x2DDACE0,
                    strong_reference_alone_proves_payload_stability=False,
                    resource_ready_flag_offset=0x50,
                    resource_completion_callback_rva=0x2DDA9E0,
                    resource_task_pair_scheduler_rva=0x2DE1670,
                    resource_no_reload_contract_verified=False,
                    resource_loader_worker_rva=0xE71380,
                    resource_completion_dispatch_rva=0xE771C0,
                    task_done_precedes_completion_callback=True,
                    resource_cache_lookup_rva=0x2DDE1C0,
                    resource_cache_insert_rva=0x2DE0C80,
                    resource_cache_uses_weak_references=True,
                    resource_completion_bookkeeping_rva=0x2DDFB20,
                    resource_cache_replace_rva=0x2DD8C80,
                    cache_replacement_releases_weak_reference=True,
                    descriptor_source_offsets=[0, 8],
                    buffer_deleter_rva=0x37C620, buffer_deallocator='CRT free',
                    strong_dispose_rva=0x37C7B0, weak_dispose_rva=0x2DE1D0,
                    control_delete_rva=0x348F50,
                    descriptor_passed_separately=True, retention_enabled=False),
                range_relocation=dict(save_function_rva=0x301820,
                    load_function_rva=0x301B10, range_entry_bytes=16,
                    encoded_pointer_bytes=16, restored_global_rvas=[0x4843ED0, 0x4843ED8],
                    uses_current_range_bases=True, allocation_lifetime_verified=False),
                stage_serialization=dict(
                    save_entry_rva=0x3841E0, load_entry_rva=0x384540,
                    save_call_rva=0x38443B, load_call_rva=0x38479E,
                    entry_calls_verified=True,
                    slot_28_function_rva=0x391CA0, slot_30_function_rva=0x391E10,
                    candidate_stride=0x40, candidate_field_offsets=[0xC, 0x1C],
                    candidate_field_bytes=4, root_field_offset=0x414,
                    root_field_bytes=2, complete_stage_coverage_verified=False),
                release_site=dict(call_rva=0x3340F4, return_rva=0x3340F9,
                                  target_rva=0x1F90000, pointer_register='RCX',
                                  type_already_overwritten=True,
                                  overwritten_vtable_rva=0x3E88D48,
                                  edx_is_allocation_size=False),
                limitations=[
                    'Static site verification does not prove complete lifetime coverage.',
                    'Environment root identity does not prove stability or lifetime of pointed-to stage data.',
                    'Allocation return precedes construction and list insertion.',
                    'Release must be correlated with a prior verified allocation or baseline; the overwritten vtable cannot identify the concrete type.',
                    'No native retention, allocator interception or pointer resurrection is enabled.',
                ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.exe.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Verified four allocation sites and shared unlink/release site; game unchanged.')


if __name__ == '__main__':
    main()

